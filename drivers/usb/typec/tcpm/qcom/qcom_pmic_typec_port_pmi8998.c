// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2026 Max Furman <taygoth@gmail.com>
 *
 * PMI8998 Type-C port handler. The Type-C detection block lives inside
 * the SMB2 USBIN region at SPMI offset 0x1300 and reports state through
 * a single consolidated "type-c-change" interrupt rather than the
 * granular IRQs that PM8150B exposes. Based on Max Furman's port;
 * this integration has only been checked offline, not on a phone.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/usb/tcpm.h>

#include <soc/qcom/qcom-spmi-pmic.h>

#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_port.h"
#include "qcom_pmic_typec_port_pmi8998.h"
#include "qcom_pmic_typec_pmi8998_limits.h"

/* Offsets and bits checked against the pinned PMI8998 smb-reg.h. */
#define TYPE_C_STATUS_1_REG			0x0b
#define TYPE_C_STATUS_4_REG			0x0e
#define TYPE_C_CFG_REG				0x58
#define TYPE_C_OR_U_USB_BIT			BIT(0)
#define VCONN_OC_CFG_BIT				BIT(1)
#define FACTORY_MODE_DETECTION_EN_BIT		BIT(5)
#define TYPE_C_CFG_2_REG			0x59
#define DFP_CC_1P4V_OR_1P6V_BIT			BIT(6)
#define EN_TRY_SOURCE_MODE_BIT			BIT(3)
#define TYPE_C_UFP_MODE_BIT			BIT(1)
#define EN_80UA_180UA_CUR_SOURCE_BIT		BIT(0)
#define TYPE_C_INTRPT_ENB_REG			0x67
/* Attach, detach, VBUS assert/deassert, CC state, debounce and VBUS error. */
#define TYPE_C_IRQ_MASK				(GENMASK(7, 5) | GENMASK(3, 0))
#define TYPE_C_INTRPT_ENB_SW_CTRL_REG		0x68
#define VCONN_EN_ORIENTATION_BIT		BIT(6)
#define TYPEC_VCONN_OVERCURR_INT_EN_BIT	BIT(5)
#define VCONN_EN_SRC_BIT			BIT(4)
#define VCONN_EN_VALUE_BIT			BIT(3)
#define UFP_EN_CMD_BIT				BIT(2)
#define DFP_EN_CMD_BIT				BIT(1)
#define TYPEC_DISABLE_CMD_BIT			BIT(0)
#define TYPEC_POWER_ROLE_CMD_MASK		GENMASK(2, 0)

struct pmi8998_typec_port {
	struct device *dev;
	struct tcpm_port *tcpm_port;
	struct regmap *regmap;
	u32 base;
	int irq;
	struct regulator *vdd_vbus;
	struct power_supply *charger;
	u32 max_sink_ua;
	/* TCPM callbacks, IRQ, lifecycle, VBUS accounting and register RMWs. */
	struct mutex lock;
	bool vbus_enabled;
	bool irq_enabled;
	bool stopping;
};

static int pmi8998_typec_port_get_cc(struct tcpc_dev *tcpc,
				     enum typec_cc_status *cc1,
				     enum typec_cc_status *cc2)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmi8998_typec_port *port = tcpm->port_priv;
	unsigned int check;
	u8 status[4];
	int ret, tries;

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;
	mutex_lock(&port->lock);
	if (port->stopping) {
		ret = -ESHUTDOWN;
		goto out;
	}
	/* No atomic snapshot exists: bound retries across orientation/role changes. */
	for (tries = 0; tries < 3; tries++) {
		ret = regmap_bulk_read(port->regmap,
				       port->base + TYPE_C_STATUS_1_REG,
				       status, sizeof(status));
		if (ret)
			goto out;
		ret = regmap_read(port->regmap, port->base + TYPE_C_STATUS_4_REG,
				  &check);
		if (ret)
			goto out;
		if (check == status[3]) {
			ret = pmi8998_decode_cc(status[0], status[1], status[3], cc1, cc2);
			goto out;
		}
	}
	ret = -EAGAIN;
out:
	mutex_unlock(&port->lock);
	return ret;
}

static int pmi8998_typec_port_set_cc(struct tcpc_dev *tcpc,
				     enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmi8998_typec_port *port = tcpm->port_priv;
	u8 power_role;
	u8 cfg2_mask = 0, cfg2_val = 0;
	int ret;

	switch (cc) {
	case TYPEC_CC_OPEN:
		power_role = TYPEC_DISABLE_CMD_BIT;
		break;
	case TYPEC_CC_RD:
		power_role = UFP_EN_CMD_BIT;
		break;
	case TYPEC_CC_RP_DEF:
		power_role = DFP_EN_CMD_BIT;
		cfg2_mask = EN_80UA_180UA_CUR_SOURCE_BIT |
			    DFP_CC_1P4V_OR_1P6V_BIT;
		cfg2_val = 0;
		break;
	case TYPEC_CC_RP_1_5:
		power_role = DFP_EN_CMD_BIT;
		cfg2_mask = EN_80UA_180UA_CUR_SOURCE_BIT |
			    DFP_CC_1P4V_OR_1P6V_BIT;
		cfg2_val = EN_80UA_180UA_CUR_SOURCE_BIT |
			   DFP_CC_1P4V_OR_1P6V_BIT;
		break;
	case TYPEC_CC_RP_3_0:
		power_role = DFP_EN_CMD_BIT;
		cfg2_mask = EN_80UA_180UA_CUR_SOURCE_BIT |
			    DFP_CC_1P4V_OR_1P6V_BIT;
		cfg2_val = EN_80UA_180UA_CUR_SOURCE_BIT;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&port->lock);
	if (port->stopping && cc != TYPEC_CC_OPEN) {
		ret = -ESHUTDOWN;
		goto out;
	}
	if (cfg2_mask) {
		int required_ua = cc == TYPEC_CC_RP_3_0 ? 3000000 :
				  cc == TYPEC_CC_RP_1_5 ? 1500000 : 500000;

		ret = regulator_get_current_limit(port->vdd_vbus);
		if (ret < 0)
			goto out;
		if (ret < required_ua) {
			ret = -EINVAL;
			goto out;
		}
		ret = regmap_update_bits(port->regmap,
					 port->base + TYPE_C_CFG_2_REG,
					 cfg2_mask, cfg2_val);
		if (ret)
			goto out;
	}

	ret = regmap_update_bits(port->regmap,
				 port->base + TYPE_C_INTRPT_ENB_SW_CTRL_REG,
				 TYPEC_POWER_ROLE_CMD_MASK, power_role);
out:
	mutex_unlock(&port->lock);
	return ret;
}

static int pmi8998_typec_port_get_vbus(struct tcpc_dev *tcpc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmi8998_typec_port *port = tcpm->port_priv;
	unsigned int status4;
	int ret;

	mutex_lock(&port->lock);
	if (port->vbus_enabled) {
		ret = 1;
	} else {
		ret = regmap_read(port->regmap, port->base + TYPE_C_STATUS_4_REG,
				  &status4);
		if (!ret)
			ret = !!(status4 & PMI8998_VBUS_PRESENT);
		else
			dev_err_ratelimited(port->dev, "VBUS status unavailable: %d\n", ret);
	}
	mutex_unlock(&port->lock);
	/* TCPM treats errors as true; never turn an I/O error into vSafe0V. */
	return ret;
}

static int pmi8998_set_input(struct pmi8998_typec_port *port, bool on)
{
	union power_supply_propval value = { .intval = on };

	return power_supply_set_property(port->charger, POWER_SUPPLY_PROP_ONLINE, &value);
}

static int pmi8998_typec_port_set_current_limit(struct tcpc_dev *tcpc, u32 ma, u32 mv)
{
	struct pmi8998_typec_port *port = tcpc_to_tcpm(tcpc)->port_priv;
	union power_supply_propval value;
	int ret;

	if ((mv != 5000 && (mv || ma)) || ma > port->max_sink_ua / 1000)
		return -EOPNOTSUPP;
	value.intval = ma * 1000;
	mutex_lock(&port->lock);
	ret = port->stopping ? -ESHUTDOWN :
		power_supply_set_property(port->charger, POWER_SUPPLY_PROP_CURRENT_MAX, &value);
	mutex_unlock(&port->lock);
	return ret;
}

static int pmi8998_typec_port_set_vbus(struct tcpc_dev *tcpc, bool on, bool sink)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmi8998_typec_port *port = tcpm->port_priv;
	int ret = 0;

	if (on && sink)
		return -EINVAL;
	mutex_lock(&port->lock);
	if (port->stopping && (on || sink)) {
		ret = -ESHUTDOWN;
		goto out;
	}
	/* Source: prove input suspension before enabling any boost output. */
	if (on) {
		ret = pmi8998_set_input(port, false);
		if (ret)
			goto out;
	}
	if (port->vbus_enabled != on) {
		ret = on ? regulator_enable(port->vdd_vbus) : regulator_disable(port->vdd_vbus);
		if (ret)
			goto out;
		port->vbus_enabled = on;
		/* Settling delay is not a voltage measurement or a vSafe0V proof. */
		usleep_range(20000, 25000);
	}
	/* Sink: a failed boost disable must never be followed by input resume. */
	if (!on) {
		ret = pmi8998_set_input(port, sink);
		if (ret)
			goto out;
	}
	if (port->tcpm_port && !port->stopping)
		tcpm_vbus_change(port->tcpm_port);
out:
	mutex_unlock(&port->lock);
	return ret;
}

static int pmi8998_typec_port_set_polarity(struct tcpc_dev *tcpc,
					   enum typec_cc_polarity pol)
{
	/* Polarity is set separately by phy-qcom-qmp-combo. */
	return 0;
}

static int pmi8998_typec_port_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmi8998_typec_port *port = tcpm->port_priv;
	unsigned int status4, value = VCONN_EN_SRC_BIT;
	int ret;

	mutex_lock(&port->lock);
	if (port->stopping && on) {
		ret = -ESHUTDOWN;
		goto out;
	}
	if (on) {
		ret = regmap_read(port->regmap, port->base + TYPE_C_STATUS_4_REG,
				  &status4);
		if (ret)
			goto out;
		if (!(status4 & PMI8998_CC_ATTACHED) ||
		    (status4 & PMI8998_VCONN_OC)) {
			ret = -EIO;
			goto out;
		}
		value |= VCONN_EN_VALUE_BIT;
		if (!(status4 & PMI8998_CC_ORIENTATION))
			value |= VCONN_EN_ORIENTATION_BIT;
	}
	ret = regmap_update_bits(port->regmap,
				 port->base + TYPE_C_INTRPT_ENB_SW_CTRL_REG,
				 VCONN_EN_SRC_BIT | VCONN_EN_VALUE_BIT |
				 VCONN_EN_ORIENTATION_BIT, value);
out:
	mutex_unlock(&port->lock);
	return ret;
}

static int pmi8998_typec_port_start_toggling(struct tcpc_dev *tcpc,
					     enum typec_port_type port_type,
					     enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pmi8998_typec_port *port = tcpm->port_priv;
	u8 role;
	int ret;

	switch (port_type) {
	case TYPEC_PORT_SRC:
		role = DFP_EN_CMD_BIT;
		break;
	case TYPEC_PORT_SNK:
		role = UFP_EN_CMD_BIT;
		break;
	case TYPEC_PORT_DRP:
		role = 0;
		break;
	default:
		return -EINVAL;
	}
	ret = pmi8998_typec_port_set_cc(tcpc, cc);
	if (ret)
		return ret;
	mutex_lock(&port->lock);
	if (port->stopping) {
		ret = -ESHUTDOWN;
		goto out;
	}
	ret = regmap_update_bits(port->regmap, port->base + TYPE_C_CFG_2_REG,
				 EN_TRY_SOURCE_MODE_BIT | TYPE_C_UFP_MODE_BIT, 0);
	if (ret)
		goto out;
	ret = regmap_update_bits(port->regmap,
				 port->base + TYPE_C_INTRPT_ENB_SW_CTRL_REG,
				 TYPEC_POWER_ROLE_CMD_MASK, role);
out:
	mutex_unlock(&port->lock);
	return ret;
}

static irqreturn_t pmi8998_typec_isr(int irq, void *dev_id)
{
	struct pmi8998_typec_port *port = dev_id;
	unsigned int status4;
	int ret;

	mutex_lock(&port->lock);
	if (port->stopping)
		goto out;
	ret = regmap_read(port->regmap, port->base + TYPE_C_STATUS_4_REG, &status4);
	if (ret) {
		dev_err_ratelimited(port->dev, "Type-C status read failed: %d\n", ret);
	} else if (status4 & PMI8998_VCONN_OC) {
		ret = regmap_update_bits(port->regmap,
					 port->base + TYPE_C_INTRPT_ENB_SW_CTRL_REG,
					 VCONN_EN_VALUE_BIT, 0);
		if (ret)
			dev_err_ratelimited(port->dev, "VCONN shutdown failed: %d\n", ret);
	}
	/* Queue current-state reads, including debounce completion and read errors. */
	tcpm_vbus_change(port->tcpm_port);
	tcpm_cc_change(port->tcpm_port);
out:
	mutex_unlock(&port->lock);
	return IRQ_HANDLED;
}

/* Best effort at every shutdown step; retain the first error for the caller. */
static int pmi8998_typec_quiesce(struct pmi8998_typec_port *port)
{
	int ret, error;

	error = regmap_write(port->regmap, port->base + TYPE_C_INTRPT_ENB_REG, 0);
	ret = regmap_update_bits(port->regmap,
				 port->base + TYPE_C_INTRPT_ENB_SW_CTRL_REG,
				 TYPEC_POWER_ROLE_CMD_MASK | VCONN_EN_VALUE_BIT |
				 VCONN_EN_SRC_BIT | TYPEC_VCONN_OVERCURR_INT_EN_BIT,
				 TYPEC_DISABLE_CMD_BIT | VCONN_EN_SRC_BIT);
	if (!error)
		error = ret;
	if (port->vbus_enabled) {
		ret = regulator_disable(port->vdd_vbus);
		if (!ret)
			port->vbus_enabled = false;
		if (!error)
			error = ret;
	}
	ret = pmi8998_set_input(port, false);
	if (!error)
		error = ret;
	return error;
}

/* Before tcpm_register_port(): its synchronous init already invokes callbacks. */
static int pmi8998_typec_port_prepare(struct pmic_typec *tcpm)
{
	struct pmi8998_typec_port *port = tcpm->port_priv;
	int ret, cleanup;

	mutex_lock(&port->lock);
	ret = pmi8998_typec_quiesce(port);
	if (ret)
		goto failed;
	msleep(200);
	ret = regmap_update_bits(port->regmap, port->base + TYPE_C_CFG_REG,
				 TYPE_C_OR_U_USB_BIT | FACTORY_MODE_DETECTION_EN_BIT |
				 VCONN_OC_CFG_BIT, 0);
	if (ret)
		goto failed;
	usleep_range(10000, 11000);
	/* Leave CC OPEN until TCPM selects an actual role. */
	mutex_unlock(&port->lock);
	return 0;
failed:
	cleanup = pmi8998_typec_quiesce(port);
	if (cleanup)
		dev_err(port->dev, "Type-C prepare rollback failed: %d\n", cleanup);
	mutex_unlock(&port->lock);
	return ret;
}

static int pmi8998_typec_port_start(struct pmic_typec *tcpm,
				    struct tcpm_port *tcpm_port)
{
	struct pmi8998_typec_port *port = tcpm->port_priv;
	int ret;

	mutex_lock(&port->lock);
	port->tcpm_port = tcpm_port;
	ret = regmap_update_bits(port->regmap,
				 port->base + TYPE_C_INTRPT_ENB_SW_CTRL_REG,
				 TYPEC_VCONN_OVERCURR_INT_EN_BIT,
				 TYPEC_VCONN_OVERCURR_INT_EN_BIT);
	if (!ret)
		ret = regmap_write(port->regmap, port->base + TYPE_C_INTRPT_ENB_REG,
				   TYPE_C_IRQ_MASK);
	if (!ret)
		port->irq_enabled = true;
	mutex_unlock(&port->lock);
	if (ret)
		return ret;
	enable_irq(port->irq);
	/* Do not lose a cable already present before unmasking the rollup. */
	tcpm_vbus_change(tcpm_port);
	tcpm_cc_change(tcpm_port);
	return 0;
}

static void pmi8998_typec_port_stop(struct pmic_typec *tcpm)
{
	struct pmi8998_typec_port *port = tcpm->port_priv;
	bool irq_enabled;
	int ret;

	mutex_lock(&port->lock);
	port->stopping = true;
	irq_enabled = port->irq_enabled;
	port->irq_enabled = false;
	mutex_unlock(&port->lock);
	/* IRQ thread takes lock too: synchronize outside it. */
	if (irq_enabled)
		disable_irq(port->irq);
	mutex_lock(&port->lock);
	port->tcpm_port = NULL;
	ret = pmi8998_typec_quiesce(port);
	mutex_unlock(&port->lock);
	if (ret)
		dev_err(port->dev, "Type-C shutdown failed: %d\n", ret);
}

static int pmi8998_check_pdos(struct device *dev, struct pmi8998_typec_port *port)
{
	struct fwnode_handle *connector;
	const char * const names[] = { "source-pdos", "sink-pdos" };
	u32 pdos[PDO_MAX_OBJECTS];
	int count, ret = 0, source_ua, i, j;

	source_ua = regulator_get_current_limit(port->vdd_vbus);
	if (source_ua <= 0)
		return source_ua ? source_ua : -EINVAL;
	connector = device_get_named_child_node(dev, "connector");
	if (!connector)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		if (!fwnode_property_present(connector, names[i]))
			continue;
		count = fwnode_property_count_u32(connector, names[i]);
		if (count <= 0 || count > ARRAY_SIZE(pdos)) {
			ret = -EINVAL;
			break;
		}
		ret = fwnode_property_read_u32_array(connector, names[i], pdos, count);
		if (ret)
			break;
		for (j = 0; j < count; j++) {
			if (!pmi8998_pdo_supported(pdos[j],
						   (i ? port->max_sink_ua : source_ua) / 1000)) {
				ret = -EOPNOTSUPP;
				break;
			}
		}
		if (ret)
			break;
	}
	fwnode_handle_put(connector);
	return ret;
}

static int pmi8998_typec_port_probe(struct platform_device *pdev,
				    struct pmic_typec *tcpm,
				    const struct pmic_typec_port_resources *res,
				    struct regmap *regmap,
				    u32 base)
{
	struct device *dev = &pdev->dev;
	struct pmi8998_typec_port *port;
	const struct qcom_spmi_pmic *pmic;
	union power_supply_propval online;
	struct device *supplier;
	int ret;

	pmic = qcom_pmic_get(dev);
	if (IS_ERR(pmic))
		return PTR_ERR(pmic);
	if (pmic->subtype != PMI8998_SUBTYPE || base != 0x1300 ||
	    (to_spmi_device(dev->parent)->usid & 1))
		return -ENODEV;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	mutex_init(&port->lock);

	port->vdd_vbus = devm_regulator_get(dev, "vdd-vbus");
	if (IS_ERR(port->vdd_vbus))
		return PTR_ERR(port->vdd_vbus);

	port->charger = devm_power_supply_get_by_reference(dev, "power-supplies");
	if (IS_ERR(port->charger))
		return dev_err_probe(dev, PTR_ERR(port->charger), "Input charger unavailable\n");
	if (!port->charger)
		return -ENODEV;
	supplier = port->charger->dev.parent;
	if (!supplier || supplier->parent != dev->parent ||
	    !of_device_is_compatible(supplier->of_node, "qcom,pmi8998-charger"))
		return -EINVAL;
	ret = power_supply_get_property(port->charger, POWER_SUPPLY_PROP_ONLINE, &online);
	if (ret)
		return dev_err_probe(dev, ret == -EAGAIN ? -EPROBE_DEFER : ret,
				     "Input charger not ready\n");
	ret = device_property_read_u32(supplier, "input-current-limit-microamp",
				       &port->max_sink_ua);
	if (ret || !port->max_sink_ua || port->max_sink_ua > 3000000)
		return -EINVAL;
	if (!device_link_add(dev, supplier, DL_FLAG_AUTOREMOVE_CONSUMER))
		return -ENOMEM;
	ret = pmi8998_check_pdos(dev, port);
	if (ret)
		return dev_err_probe(dev, ret, "Only bounded fixed 5 V PDOs are implemented\n");

	port->dev = dev;
	port->regmap = regmap;
	port->base = base;

	port->irq = platform_get_irq_byname(pdev, "type-c-change");
	if (port->irq < 0)
		return port->irq;

	ret = devm_request_threaded_irq(dev, port->irq, NULL,
					pmi8998_typec_isr,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					"type-c-change", port);
	if (ret)
		return ret;

	tcpm->port_priv = port;

	tcpm->tcpc.get_vbus = pmi8998_typec_port_get_vbus;
	tcpm->tcpc.set_vbus = pmi8998_typec_port_set_vbus;
	tcpm->tcpc.set_current_limit = pmi8998_typec_port_set_current_limit;
	tcpm->tcpc.set_cc = pmi8998_typec_port_set_cc;
	tcpm->tcpc.get_cc = pmi8998_typec_port_get_cc;
	tcpm->tcpc.set_polarity = pmi8998_typec_port_set_polarity;
	tcpm->tcpc.set_vconn = pmi8998_typec_port_set_vconn;
	tcpm->tcpc.start_toggling = pmi8998_typec_port_start_toggling;

	tcpm->port_prepare = pmi8998_typec_port_prepare;
	tcpm->port_start = pmi8998_typec_port_start;
	tcpm->port_stop = pmi8998_typec_port_stop;

	return 0;
}

const struct pmic_typec_port_resources pmi8998_port_res = {
	.probe = pmi8998_typec_port_probe,
};

#if IS_ENABLED(CONFIG_TYPEC_QCOM_PMIC_KUNIT_TEST)
#include "qcom_pmic_typec_port_pmi8998_test.c"
#endif

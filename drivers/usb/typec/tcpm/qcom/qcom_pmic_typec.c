// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec_mux.h>

#include <drm/bridge/aux-bridge.h>

#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_pdphy.h"
#include "qcom_pmic_typec_port.h"

struct pmic_typec_resources {
	const struct pmic_typec_pdphy_resources	*pdphy_res;
	const struct pmic_typec_port_resources	*port_res;
};

static int qcom_pmic_typec_init(struct tcpc_dev *tcpc)
{
	return 0;
}

static int qcom_pmic_typec_probe(struct platform_device *pdev)
{
	struct pmic_typec *tcpm;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct pmic_typec_resources *res;
	struct regmap *regmap;
	struct auxiliary_device *bridge_dev;
	bool port_prepared = false, pdphy_prepared = false;
	bool port_started = false, pdphy_started = false;
	bool registered = false;
	u32 base;
	int ret;

	res = of_device_get_match_data(dev);
	if (!res)
		return -ENODEV;

	tcpm = devm_kzalloc(dev, sizeof(*tcpm), GFP_KERNEL);
	if (!tcpm)
		return -ENOMEM;

	tcpm->dev = dev;
	tcpm->tcpc.init = qcom_pmic_typec_init;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		dev_err(dev, "Failed to get regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_index(np, "reg", 0, &base);
	if (ret)
		return ret;

	ret = res->port_res->probe(pdev, tcpm,
				   res->port_res, regmap, base);
	if (ret)
		return ret;

	if (res->pdphy_res) {
		ret = of_property_read_u32_index(np, "reg", 1, &base);
		if (ret)
			return ret;

		ret = qcom_pmic_typec_pdphy_probe(pdev, tcpm,
						  res->pdphy_res, regmap, base);
		if (ret)
			return ret;
	} else {
		ret = qcom_pmic_typec_pdphy_stub_probe(pdev, tcpm);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, tcpm);

	tcpm->tcpc.fwnode = device_get_named_child_node(tcpm->dev, "connector");
	if (!tcpm->tcpc.fwnode)
		return -EINVAL;

	bridge_dev = devm_drm_dp_hpd_bridge_alloc(tcpm->dev, to_of_node(tcpm->tcpc.fwnode));
	if (IS_ERR(bridge_dev)) {
		ret = PTR_ERR(bridge_dev);
		goto cleanup;
	}

	if (tcpm->port_prepare) {
		port_prepared = true;
		ret = tcpm->port_prepare(tcpm);
		if (ret)
			goto cleanup;
	}
	if (tcpm->pdphy_prepare) {
		pdphy_prepared = true;
		ret = tcpm->pdphy_prepare(tcpm);
		if (ret)
			goto cleanup;
	}

	tcpm->tcpm_port = tcpm_register_port(tcpm->dev, &tcpm->tcpc);
	if (IS_ERR(tcpm->tcpm_port)) {
		ret = PTR_ERR(tcpm->tcpm_port);
		goto cleanup;
	}
	registered = true;
	ret = tcpm->port_start(tcpm, tcpm->tcpm_port);
	if (ret)
		goto cleanup;
	port_started = true;
	ret = tcpm->pdphy_start(tcpm, tcpm->tcpm_port);
	if (ret)
		goto cleanup;
	pdphy_started = true;
	ret = devm_drm_dp_hpd_bridge_add(tcpm->dev, bridge_dev);
	if (ret)
		goto cleanup;
	return 0;

cleanup:
	/* Block/drain all producers before unregistering their TCPM consumer. */
	if (pdphy_prepared || pdphy_started)
		tcpm->pdphy_stop(tcpm);
	if (port_prepared || port_started)
		tcpm->port_stop(tcpm);
	if (registered)
		tcpm_unregister_port(tcpm->tcpm_port);
	fwnode_handle_put(tcpm->tcpc.fwnode);
	return ret;
}

static void qcom_pmic_typec_remove(struct platform_device *pdev)
{
	struct pmic_typec *tcpm = platform_get_drvdata(pdev);

	tcpm->pdphy_stop(tcpm);
	tcpm->port_stop(tcpm);
	tcpm_unregister_port(tcpm->tcpm_port);
	fwnode_handle_put(tcpm->tcpc.fwnode);
}

static const struct pmic_typec_resources pm8150b_typec_res = {
	.pdphy_res = &pm8150b_pdphy_res,
	.port_res = &pm8150b_port_res,
};

static const struct pmic_typec_resources pmi632_typec_res = {
	/* PD PHY not present */
	.port_res = &pm8150b_port_res,
};

static const struct pmic_typec_resources pmi8998_typec_res = {
	/* Message/control offsets match the downstream PMI8998 PD PHY. */
	.pdphy_res = &pm8150b_pdphy_res,
	.port_res  = &pmi8998_port_res,
};

static const struct of_device_id qcom_pmic_typec_table[] = {
	{ .compatible = "qcom,pm8150b-typec", .data = &pm8150b_typec_res },
	{ .compatible = "qcom,pmi632-typec", .data = &pmi632_typec_res },
	{ .compatible = "qcom,pmi8998-typec", .data = &pmi8998_typec_res },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_pmic_typec_table);

static struct platform_driver qcom_pmic_typec_driver = {
	.driver = {
		.name = "qcom,pmic-typec",
		.of_match_table = qcom_pmic_typec_table,
	},
	.probe = qcom_pmic_typec_probe,
	.remove = qcom_pmic_typec_remove,
};

module_platform_driver(qcom_pmic_typec_driver);

MODULE_DESCRIPTION("QCOM PMIC USB Type-C Port Manager Driver");
MODULE_LICENSE("GPL");

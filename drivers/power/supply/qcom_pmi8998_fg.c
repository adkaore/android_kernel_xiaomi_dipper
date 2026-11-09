// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019 XiaoMi, Inc.
 * Direct telemetry derived from the GPLv2 FG GEN3 register interface.
 * No SRAM/IMA/DMA, profile, calibration, charger or JEITA programming.
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <soc/qcom/qcom-spmi-pmic.h>

#include "qcom_pmi8998_fg.h"

#define FG_SOC_RT_STS		0x10
#define FG_SOC_READY		BIT(1)
#define FG_SOC_ENABLE		0x46
#define FG_ALGORITHM_EN		BIT(7)
#define FG_MONOTONIC_SOC	0x09
#define FG_BATT_TEMP_STS	0x106
#define FG_TEMP_AVAILABLE	BIT(0)
#define FG_BATT_RT_STS		0x110
#define FG_BATT_MISSING		BIT(3)
#define FG_BATT_TEMP		0x150
#define FG_BATT_VOLTAGE		0x1a0
#define FG_BATT_CURRENT		0x1a2

struct pmi8998_fg {
	struct regmap *regmap;
	struct power_supply *psy;
	unsigned int major;
	u32 base;
};

static int pmi8998_fg_read(struct pmi8998_fg *fg, unsigned int offset,
			   void *value, size_t size)
{
	return regmap_bulk_read(fg->regmap, fg->base + offset, value, size);
}

static int pmi8998_fg_present(struct pmi8998_fg *fg, int *present)
{
	u8 status;
	int ret;

	ret = pmi8998_fg_read(fg, FG_BATT_RT_STS, &status, 1);
	if (!ret)
		*present = !(status & FG_BATT_MISSING);
	return ret;
}

static int pmi8998_fg_ready(struct pmi8998_fg *fg)
{
	u8 status;
	int present, ret;

	ret = pmi8998_fg_present(fg, &present);
	if (ret)
		return ret;
	if (!present)
		return -ENODATA;
	ret = pmi8998_fg_read(fg, FG_SOC_ENABLE, &status, 1);
	if (ret)
		return ret;
	if (!(status & FG_ALGORITHM_EN))
		return -EAGAIN;
	ret = pmi8998_fg_read(fg, FG_SOC_RT_STS, &status, 1);
	if (ret)
		return ret;
	return status & FG_SOC_READY ? 0 : -EAGAIN;
}

static int pmi8998_fg_get_capacity(struct pmi8998_fg *fg, int *value)
{
	u8 data[2];
	int tries, ret;

	/* Reading a changing primary/copy pair must not fabricate a lower SOC. */
	for (tries = 0; tries < 5; tries++) {
		ret = pmi8998_fg_read(fg, FG_MONOTONIC_SOC, data, sizeof(data));
		if (ret)
			return ret;
		if (data[0] == data[1]) {
			*value = pmi8998_fg_capacity(data[0]);
			return 0;
		}
	}
	return -EAGAIN;
}

static int pmi8998_fg_get_property(struct power_supply *psy,
				   enum power_supply_property property,
				   union power_supply_propval *value)
{
	struct pmi8998_fg *fg = power_supply_get_drvdata(psy);
	u8 data[2], status;
	unsigned int raw;
	int ret;

	if (property == POWER_SUPPLY_PROP_PRESENT)
		return pmi8998_fg_present(fg, &value->intval);
	ret = pmi8998_fg_ready(fg);
	if (ret)
		return ret;

	switch (property) {
	case POWER_SUPPLY_PROP_STATUS:
		/* Charger state, not a guess from SOC/current or USB presence. */
		return power_supply_get_property_from_supplier(psy, property, value);
	case POWER_SUPPLY_PROP_CAPACITY:
		return pmi8998_fg_get_capacity(fg, &value->intval);
	case POWER_SUPPLY_PROP_TEMP:
		ret = pmi8998_fg_read(fg, FG_BATT_TEMP_STS, &status, 1);
		if (ret)
			return ret;
		if (!(status & FG_TEMP_AVAILABLE))
			return -ENODATA;
		ret = pmi8998_fg_read(fg, FG_BATT_TEMP, data, sizeof(data));
		if (!ret)
			value->intval = pmi8998_fg_temperature(data);
		return ret;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = pmi8998_fg_read(fg, property == POWER_SUPPLY_PROP_VOLTAGE_NOW ?
				      FG_BATT_VOLTAGE : FG_BATT_CURRENT, data, sizeof(data));
		if (ret)
			return ret;
		raw = pmi8998_fg_word(data, fg->major);
		if (property == POWER_SUPPLY_PROP_VOLTAGE_NOW)
			value->intval = pmi8998_fg_voltage(raw);
		else
			value->intval = pmi8998_fg_current(raw);
		return 0;
	default:
		return -EINVAL;
	}
}

static void pmi8998_fg_external_power_changed(struct power_supply *psy)
{
	power_supply_changed(psy);
}

static const enum power_supply_property pmi8998_fg_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
};

static const struct power_supply_desc pmi8998_fg_desc = {
	.name = "pmi8998-bms",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = pmi8998_fg_properties,
	.num_properties = ARRAY_SIZE(pmi8998_fg_properties),
	.get_property = pmi8998_fg_get_property,
	.external_power_changed = pmi8998_fg_external_power_changed,
};

static irqreturn_t pmi8998_fg_irq(int irq, void *data)
{
	struct pmi8998_fg *fg = data;

	power_supply_changed(fg->psy);
	return IRQ_HANDLED;
}

static int pmi8998_fg_probe(struct platform_device *pdev)
{
	static const char * const irq_names[] = {
		"soc-ready", "soc-delta", "batt-missing", "batt-temp",
	};
	struct device *dev = &pdev->dev;
	const struct qcom_spmi_pmic *pmic;
	struct power_supply_config config = {};
	struct pmi8998_fg *fg;
	int i, irq, ret;

	pmic = qcom_pmic_get(dev);
	if (IS_ERR(pmic))
		return dev_err_probe(dev, PTR_ERR(pmic), "Missing parent PMIC\n");
	if (pmic->subtype != PMI8998_SUBTYPE || (to_spmi_device(dev->parent)->usid & 1))
		return -ENODEV;
	/* The downstream byte-order rules are known for digital major 1 and 2. */
	if (pmic->major != 1 && pmic->major != 2)
		return dev_err_probe(dev, -ENODEV, "Unsupported FG digital revision\n");
	fg = devm_kzalloc(dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;
	fg->major = pmic->major;
	fg->regmap = dev_get_regmap(dev->parent, NULL);
	if (!fg->regmap)
		return -ENODEV;
	ret = device_property_read_u32(dev, "reg", &fg->base);
	if (ret || fg->base != 0x4000)
		return -EINVAL;
	config.drv_data = fg;
	config.fwnode = dev_fwnode(dev);
	fg->psy = devm_power_supply_register(dev, &pmi8998_fg_desc, &config);
	if (IS_ERR(fg->psy))
		return dev_err_probe(dev, PTR_ERR(fg->psy), "Failed to register FG telemetry\n");
	/* Managed IRQs are freed before the managed supply can disappear. */
	for (i = 0; i < ARRAY_SIZE(irq_names); i++) {
		irq = platform_get_irq_byname(pdev, irq_names[i]);
		if (irq < 0)
			return irq;
		ret = devm_request_threaded_irq(dev, irq, NULL, pmi8998_fg_irq,
						IRQF_ONESHOT, dev_name(dev), fg);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct of_device_id pmi8998_fg_match[] = {
	{ .compatible = "qcom,pmi8998-fg" },
	{ }
};
MODULE_DEVICE_TABLE(of, pmi8998_fg_match);

static struct platform_driver pmi8998_fg_driver = {
	.probe = pmi8998_fg_probe,
	.driver = {
		.name = "qcom-pmi8998-fg",
		.of_match_table = pmi8998_fg_match,
	},
};
module_platform_driver(pmi8998_fg_driver);

MODULE_DESCRIPTION("Read-only PMI8998 GEN3 fuel gauge telemetry");
MODULE_LICENSE("GPL");

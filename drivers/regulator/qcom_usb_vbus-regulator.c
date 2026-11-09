// SPDX-License-Identifier: GPL-2.0-only
//
// Qualcomm PMIC VBUS output regulator driver
//
// Copyright (c) 2020, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>
#include <linux/spmi.h>

#include <soc/qcom/qcom-spmi-pmic.h>

#define CMD_OTG				0x40
#define OTG_EN				BIT(0)
#define OTG_CURRENT_LIMIT_CFG		0x52
#define OTG_CURRENT_LIMIT_MASK		GENMASK(2, 0)
#define OTG_CFG				0x53
#define OTG_EN_SRC_CFG			BIT(1)
#define PMI8998_OTG_STATUS		0x09
#define PMI8998_BOOST_SOFTSTART_DONE	BIT(3)
#define PMI8998_OTG_STATE_MASK		GENMASK(2, 0)
#define PMI8998_OTG_STATE_ENABLED		0x2
#define PMI8998_OTG_ENG_CFG		0xc0
#define PMI8998_HALT_1_IN_8		BIT(0)
#define PMI8998_SEC_ACCESS		0xd0

static const unsigned int curr_table[] = {
	500000, 1000000, 1500000, 2000000, 2500000, 3000000,
};

/* PMI8998 is NOT PM8150B-compatible for current-limit selector values. */
static const unsigned int pmi8998_curr_table[] = {
	250000, 500000, 750000, 1000000, 1250000, 1500000,
};

static int pmi8998_vbus_halt_reg(struct regmap *regmap, unsigned int base, bool halt)
{
	int ret;

	ret = regmap_write(regmap, base + PMI8998_SEC_ACCESS, 0xa5);
	if (ret)
		return ret;
	return regmap_update_bits(regmap, base + PMI8998_OTG_ENG_CFG,
				  PMI8998_HALT_1_IN_8, halt ? PMI8998_HALT_1_IN_8 : 0);
}

static int pmi8998_vbus_halt(struct regulator_dev *rdev, bool halt)
{
	return pmi8998_vbus_halt_reg(rdev_get_regmap(rdev),
				    rdev->desc->enable_reg - CMD_OTG, halt);
}

static int pmi8998_vbus_disable(struct regulator_dev *rdev)
{
	int ret;

	ret = regulator_disable_regmap(rdev);
	if (ret)
		return ret;
	return pmi8998_vbus_halt(rdev, false);
}

static int pmi8998_vbus_enable(struct regulator_dev *rdev)
{
	unsigned int base = rdev->desc->enable_reg - CMD_OTG;
	unsigned int status;
	int ret, cleanup;

	ret = pmi8998_vbus_halt(rdev, true);
	if (ret)
		return ret;
	ret = regulator_enable_regmap(rdev);
	if (ret)
		goto rollback;
	ret = regmap_read_poll_timeout(rdev_get_regmap(rdev),
				       base + PMI8998_OTG_STATUS, status,
				       (status & PMI8998_BOOST_SOFTSTART_DONE) &&
				       (status & PMI8998_OTG_STATE_MASK) ==
				       PMI8998_OTG_STATE_ENABLED, 500, 100000);
	if (!ret)
		return 0;
rollback:
	cleanup = pmi8998_vbus_disable(rdev);
	if (cleanup)
		dev_err(rdev_get_dev(rdev), "VBUS rollback failed: %d\n", cleanup);
	return ret;
}

static const struct regulator_ops pmi8998_usb_vbus_reg_ops = {
	.enable = pmi8998_vbus_enable,
	.disable = pmi8998_vbus_disable,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
};

static const struct regulator_ops qcom_usb_vbus_reg_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
};

static const struct regulator_desc qcom_usb_vbus_rdesc = {
	.name = "usb_vbus",
	.ops = &qcom_usb_vbus_reg_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.curr_table = curr_table,
	.n_current_limits = ARRAY_SIZE(curr_table),
};

static const struct regulator_desc pmi8998_usb_vbus_rdesc = {
	.name = "usb_vbus",
	.ops = &pmi8998_usb_vbus_reg_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.curr_table = pmi8998_curr_table,
	.n_current_limits = ARRAY_SIZE(pmi8998_curr_table),
};

static int qcom_usb_vbus_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	const struct regulator_desc *template;
	const struct qcom_spmi_pmic *pmic;
	struct regulator_desc *desc;
	int ret;
	u32 base;

	ret = of_property_read_u32(dev->of_node, "reg", &base);
	if (ret < 0) {
		dev_err(dev, "no base address found\n");
		return ret;
	}

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		dev_err(dev, "Failed to get regmap\n");
		return -ENOENT;
	}
	template = device_get_match_data(dev);
	if (!template)
		return -ENODEV;
	if (template == &pmi8998_usb_vbus_rdesc) {
		pmic = qcom_pmic_get(dev);
		if (IS_ERR(pmic))
			return PTR_ERR(pmic);
		if (pmic->subtype != PMI8998_SUBTYPE || base != 0x1100 ||
		    (to_spmi_device(dev->parent)->usid & 1))
			return -ENODEV;
		/* Do not inherit an unaccounted bootloader boost vote. */
		ret = regmap_update_bits(regmap, base + CMD_OTG, OTG_EN, 0);
		if (ret)
			return ret;
		ret = pmi8998_vbus_halt_reg(regmap, base, false);
		if (ret)
			return ret;
	}
	/* Register offsets must never mutate a descriptor shared by other PMICs. */
	desc = devm_kmemdup(dev, template, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	init_data = of_get_regulator_init_data(dev, dev->of_node,
					       desc);
	if (!init_data)
		return -ENOMEM;

	desc->enable_reg = base + CMD_OTG;
	desc->enable_mask = OTG_EN;
	desc->csel_reg = base + OTG_CURRENT_LIMIT_CFG;
	desc->csel_mask = OTG_CURRENT_LIMIT_MASK;
	config.dev = dev;
	config.init_data = init_data;
	config.of_node = dev->of_node;
	config.regmap = regmap;

	/* Establish ownership before publishing the regulator to consumers. */
	ret = regmap_update_bits(regmap, base + OTG_CFG, OTG_EN_SRC_CFG, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to select software VBUS control\n");
	rdev = devm_regulator_register(dev, desc, &config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(dev, "not able to register vbus reg %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id qcom_usb_vbus_regulator_match[] = {
	{ .compatible = "qcom,pm8150b-vbus-reg", .data = &qcom_usb_vbus_rdesc },
	{ .compatible = "qcom,pmi8998-vbus-reg", .data = &pmi8998_usb_vbus_rdesc },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_usb_vbus_regulator_match);

static struct platform_driver qcom_usb_vbus_regulator_driver = {
	.driver		= {
		.name	= "qcom-usb-vbus-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = qcom_usb_vbus_regulator_match,
	},
	.probe		= qcom_usb_vbus_regulator_probe,
};
module_platform_driver(qcom_usb_vbus_regulator_driver);

MODULE_DESCRIPTION("Qualcomm USB vbus regulator driver");
MODULE_LICENSE("GPL v2");

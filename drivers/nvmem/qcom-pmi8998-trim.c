// SPDX-License-Identifier: GPL-2.0-only
/* Expose only PMI8998's factory RC19P2 trim byte; never write PMIC registers. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <soc/qcom/qcom-spmi-pmic.h>

struct pmi8998_trim {
	struct regmap *regmap;
	u32 address;
};

static int pmi8998_trim_read(void *priv, unsigned int offset, void *value, size_t bytes)
{
	struct pmi8998_trim *trim = priv;

	if (offset || bytes != 1)
		return -EINVAL;
	return regmap_bulk_read(trim->regmap, trim->address, value, 1);
}

static int pmi8998_trim_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_spmi_pmic *pmic;
	struct pmi8998_trim *trim;
	struct nvmem_device *nvmem;
	struct nvmem_config config = {
		.dev = dev,
		.owner = THIS_MODULE,
		.id = NVMEM_DEVID_AUTO,
		.name = "pmi8998-rc-trim",
		.size = 1,
		.word_size = 1,
		.stride = 1,
		.read_only = true,
		.root_only = true,
		.reg_read = pmi8998_trim_read,
	};
	int ret;

	pmic = qcom_pmic_get(dev);
	if (IS_ERR(pmic))
		return PTR_ERR(pmic);
	if (pmic->subtype != PMI8998_SUBTYPE)
		return -ENODEV;
	if (to_spmi_device(dev->parent)->usid & 1)
		return -ENODEV;
	trim = devm_kzalloc(dev, sizeof(*trim), GFP_KERNEL);
	if (!trim)
		return -ENOMEM;
	trim->regmap = dev_get_regmap(dev->parent, NULL);
	if (!trim->regmap)
		return -ENODEV;
	ret = device_property_read_u32(dev, "reg", &trim->address);
	if (ret || trim->address != 0x9f3)
		return -EINVAL;
	config.priv = trim;
	nvmem = devm_nvmem_register(dev, &config);
	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id pmi8998_trim_match[] = {
	{ .compatible = "qcom,pmi8998-rc-trim" },
	{ }
};
MODULE_DEVICE_TABLE(of, pmi8998_trim_match);

static struct platform_driver pmi8998_trim_driver = {
	.probe = pmi8998_trim_probe,
	.driver = {
		.name = "qcom-pmi8998-trim",
		.of_match_table = pmi8998_trim_match,
	},
};
module_platform_driver(pmi8998_trim_driver);

MODULE_DESCRIPTION("Read-only PMI8998 factory RC oscillator trim");
MODULE_LICENSE("GPL");

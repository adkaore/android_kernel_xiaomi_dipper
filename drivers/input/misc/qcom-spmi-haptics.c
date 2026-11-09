// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Caleb Connolly <caleb@connolly.tech>
 * Copyright (c) 2014-2015, 2017, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018 XiaoMi, Inc.
 * PMI8998 buffer-mode LRA driver, using ff-memless rather than LED sysfs.
 * Register programming adapted from the pinned Qualcomm downstream driver.
 */
#include <dt-bindings/input/qcom,spmi-haptics.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/spmi.h>
#include <linux/workqueue.h>
#include <soc/qcom/qcom-spmi-pmic.h>

#include "qcom-spmi-haptics.h"

#define HAP_STATUS		0x0a
#define HAP_SHORT		BIT(3)
#define HAP_AUTO_ERROR		BIT(4)
#define HAP_EN_CTL		0x46
#define HAP_ENABLE		BIT(7)
#define HAP_EN_CTL2		0x48
#define HAP_BRAKE_ENABLE	BIT(0)
#define HAP_CFG1		0x4c
#define HAP_CFG2		0x4d
#define HAP_SEL			0x4e
#define HAP_SOURCE		GENMASK(5, 4)
#define HAP_LRA_AUTO_RES		0x4f
#define HAP_AUTO_MODE		GENMASK(6, 4)
#define HAP_HIGH_Z		GENMASK(3, 2)
#define HAP_CAL_PERIOD		GENMASK(1, 0)
#define HAP_VMAX		0x51
#define HAP_VMAX_MASK		GENMASK(5, 1)
#define HAP_OVERDRIVE		BIT(6)
#define HAP_ILIM		0x52
#define HAP_SC_DEBOUNCE		0x53
#define HAP_RATE_LO		0x54
#define HAP_RATE_HI		0x55
#define HAP_SC_CLEAR		0x59
#define HAP_BRAKE		0x5c
#define HAP_REPEAT		0x5e
#define HAP_SAMPLES		0x60
#define HAP_PLAY		0x70
#define HAP_PLAY_ENABLE		BIT(7)
#define HAP_UNLOCK		0xd0
#define HAP_TEST2		0xe3
#define HAP_AUTO_ENABLE		BIT(7)

struct spmi_haptics {
	struct device *dev;
	struct regmap *regmap;
	struct input_dev *input;
	struct work_struct work;
	struct delayed_work auto_work;
	/* Serialize every register access, including secure unlock + write. */
	struct mutex lock;
	/* The memless callback holds an input spinlock and must not sleep. */
	spinlock_t request_lock;
	unsigned int magnitude;
	bool accepting;
	bool active;
	bool auto_enabled;
	bool faulted;
	bool irq_disabled;
	/* These three are serialized by input->mutex. */
	bool opened;
	bool suspended;
	bool removing;
	u32 base;
	u32 max_mv;
	u32 rate;
	u32 ilim;
	u32 debounce;
	u32 auto_mode;
	u32 high_z;
	u32 cal_period;
	u8 brake;
	int sc_irq;
};

static int qhap_update(struct spmi_haptics *h, unsigned int reg,
		       unsigned int mask, unsigned int value)
{
	return regmap_update_bits(h->regmap, h->base + reg, mask, value);
}

static int qhap_auto_res(struct spmi_haptics *h, bool enable)
{
	int ret;

	lockdep_assert_held(&h->lock);
	ret = regmap_write(h->regmap, h->base + HAP_UNLOCK, 0xa5);
	if (ret)
		return ret;
	return qhap_update(h, HAP_TEST2, HAP_AUTO_ENABLE, enable ? HAP_AUTO_ENABLE : 0);
}

/* Always attempt module disable, even when clearing PLAY fails. */
static int qhap_stop(struct spmi_haptics *h)
{
	int first, ret;

	lockdep_assert_held(&h->lock);
	h->active = false;
	h->auto_enabled = false;
	cancel_delayed_work(&h->auto_work);
	first = regmap_write(h->regmap, h->base + HAP_PLAY, 0);
	ret = qhap_update(h, HAP_EN_CTL, HAP_ENABLE, 0);
	if (!first)
		first = ret;
	ret = qhap_auto_res(h, false);
	return first ? first : ret;
}

static void qhap_gate(struct spmi_haptics *h, bool accepting)
{
	unsigned long flags;

	spin_lock_irqsave(&h->request_lock, flags);
	h->accepting = accepting;
	h->magnitude = 0;
	spin_unlock_irqrestore(&h->request_lock, flags);
}

/* A bus/short/autoresonance fault is latched until driver rebind, not retried. */
static void qhap_fault(struct spmi_haptics *h, int error)
{
	int ret;

	lockdep_assert_held(&h->lock);
	qhap_gate(h, false);
	if (!h->irq_disabled && h->sc_irq >= 0) {
		disable_irq_nosync(h->sc_irq);
		h->irq_disabled = true;
	}
	h->faulted = true;
	ret = qhap_stop(h);
	dev_err_ratelimited(h->dev, "Haptics fault %d, stop status %d\n", error, ret);
}

static void qhap_auto_work(struct work_struct *work)
{
	struct spmi_haptics *h = container_of(to_delayed_work(work),
					    struct spmi_haptics, auto_work);
	unsigned int status;
	unsigned long flags;
	bool accepting;
	int ret;

	guard(mutex)(&h->lock);
	spin_lock_irqsave(&h->request_lock, flags);
	accepting = h->accepting;
	spin_unlock_irqrestore(&h->request_lock, flags);
	if (!accepting || !h->active || h->faulted)
		return;
	ret = regmap_read(h->regmap, h->base + HAP_STATUS, &status);
	if (!ret && (status & (HAP_SHORT | HAP_AUTO_ERROR)))
		ret = -EIO;
	if (!ret && !h->auto_enabled)
		ret = qhap_auto_res(h, true);
	if (ret) {
		qhap_fault(h, ret);
	} else {
		h->auto_enabled = true;
		mod_delayed_work(system_wq, &h->auto_work, msecs_to_jiffies(20));
	}
}

static void qhap_work(struct work_struct *work)
{
	struct spmi_haptics *h = container_of(work, struct spmi_haptics, work);
	unsigned long flags;
	unsigned int magnitude, status;
	int ret;

	spin_lock_irqsave(&h->request_lock, flags);
	magnitude = h->accepting ? h->magnitude : 0;
	spin_unlock_irqrestore(&h->request_lock, flags);

	guard(mutex)(&h->lock);
	if (h->faulted)
		return;
	if (!magnitude) {
		ret = qhap_stop(h);
		goto done;
	}
	if (!h->active) {
		ret = regmap_read(h->regmap, h->base + HAP_STATUS, &status);
		if (!ret && (status & HAP_SHORT))
			ret = -EIO;
		if (ret)
			goto done;
	}
	ret = qhap_update(h, HAP_VMAX, HAP_VMAX_MASK | HAP_OVERDRIVE,
			  FIELD_PREP(HAP_VMAX_MASK, qhap_vmax_code(magnitude, h->max_mv)));
	if (ret || h->active)
		goto done;
	ret = qhap_auto_res(h, false);
	if (!ret)
		ret = qhap_update(h, HAP_EN_CTL, HAP_ENABLE, HAP_ENABLE);
	if (!ret)
		ret = regmap_write(h->regmap, h->base + HAP_PLAY, HAP_PLAY_ENABLE);
	if (!ret) {
		h->active = true;
		/* Do not prolong short UI effects by sleeping in the play worker. */
		mod_delayed_work(system_wq, &h->auto_work, msecs_to_jiffies(20));
	}
done:
	if (ret)
		qhap_fault(h, ret);
}

static int qhap_play(struct input_dev *input, void *data, struct ff_effect *effect)
{
	struct spmi_haptics *h = input_get_drvdata(input);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&h->request_lock, flags);
	if (h->accepting) {
		h->magnitude = max_t(unsigned int, effect->u.rumble.strong_magnitude,
				     effect->u.rumble.weak_magnitude / 2);
		/* Queue under the gate lock: no enqueue after quiesce's cancel. */
		schedule_work(&h->work);
	} else {
		ret = -EHOSTDOWN;
	}
	spin_unlock_irqrestore(&h->request_lock, flags);
	return ret;
}

static int qhap_quiesce(struct spmi_haptics *h)
{
	int ret;

	qhap_gate(h, false);
	cancel_work_sync(&h->work);
	cancel_delayed_work_sync(&h->auto_work);
	guard(mutex)(&h->lock);
	ret = qhap_stop(h);
	if (ret)
		h->faulted = true;
	return ret;
}

static int qhap_open(struct input_dev *input)
{
	struct spmi_haptics *h = input_get_drvdata(input);

	if (h->removing || h->suspended)
		return -EHOSTDOWN;
	guard(mutex)(&h->lock);
	if (h->faulted)
		return -EIO;
	h->opened = true;
	qhap_gate(h, true);
	return 0;
}

static void qhap_close(struct input_dev *input)
{
	struct spmi_haptics *h = input_get_drvdata(input);
	int ret;

	h->opened = false;
	ret = qhap_quiesce(h);
	if (ret)
		dev_err(h->dev, "Failed to stop haptics on close: %d\n", ret);
}

static irqreturn_t qhap_short_irq(int irq, void *data)
{
	struct spmi_haptics *h = data;
	unsigned int status;
	int ret;

	guard(mutex)(&h->lock);
	ret = regmap_read(h->regmap, h->base + HAP_STATUS, &status);
	if (ret || (status & HAP_SHORT)) {
		qhap_fault(h, ret ? ret : -EIO);
		/* Clearing the latch is not permission to restart a faulted motor. */
		ret = regmap_write(h->regmap, h->base + HAP_SC_CLEAR, BIT(0));
		if (ret)
			dev_err_ratelimited(h->dev, "Failed to clear short latch: %d\n", ret);
	}
	return IRQ_HANDLED;
}

static int qhap_configure(struct spmi_haptics *h)
{
	static const u8 samples[8] = { 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e };
	int ret;

	guard(mutex)(&h->lock);
	ret = qhap_stop(h);
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_CFG1, BIT(0), HAP_TYPE_LRA);
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_LRA_AUTO_RES, HAP_AUTO_MODE | HAP_HIGH_Z | HAP_CAL_PERIOD,
			  FIELD_PREP(HAP_AUTO_MODE, h->auto_mode) |
			  FIELD_PREP(HAP_HIGH_Z, h->high_z) |
			  FIELD_PREP(HAP_CAL_PERIOD, ilog2(h->cal_period / 4)));
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_SEL, HAP_SOURCE, FIELD_PREP(HAP_SOURCE, HAP_PLAY_BUFFER));
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_ILIM, BIT(0), h->ilim == 800);
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_SC_DEBOUNCE, GENMASK(2, 0), qhap_debounce_code(h->debounce));
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_CFG2, BIT(0), HAP_WAVE_SINE);
	if (ret)
		return ret;
	ret = regmap_write(h->regmap, h->base + HAP_RATE_LO, h->rate & 0xff);
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_RATE_HI, GENMASK(3, 0), h->rate >> 8);
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_EN_CTL2, HAP_BRAKE_ENABLE, HAP_BRAKE_ENABLE);
	if (ret)
		return ret;
	ret = regmap_write(h->regmap, h->base + HAP_BRAKE, h->brake);
	if (ret)
		return ret;
	ret = qhap_update(h, HAP_REPEAT, GENMASK(6, 4) | GENMASK(1, 0), 0);
	if (ret)
		return ret;
	return regmap_bulk_write(h->regmap, h->base + HAP_SAMPLES, samples, sizeof(samples));
}

static int qhap_properties(struct spmi_haptics *h)
{
	struct device *dev = h->dev;
	u32 value, period, brake[4];
	u8 trim;
	int i, ret;

	ret = device_property_read_u32(dev, "reg", &h->base);
	if (ret || h->base != 0xc000)
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,vmax-mv", &h->max_mv);
	if (ret || h->max_mv < 116 || h->max_mv > 3596)
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,wave-play-rate-us", &period);
	if (ret || period < 5 || period > 20475)
		return -EINVAL;
	ret = nvmem_cell_read_u8(dev, "rc-trim", &trim);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read factory RC trim\n");
	h->rate = qhap_rate_code(period, trim);
	if (!h->rate || h->rate > 0xfff)
		return -ERANGE;
	ret = device_property_read_u32(dev, "qcom,actuator-type", &value);
	if (ret || value != HAP_TYPE_LRA)
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,play-mode", &value);
	if (ret || value != HAP_PLAY_BUFFER)
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,wave-shape", &value);
	if (ret || value != HAP_WAVE_SINE)
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,ilim-ma", &h->ilim);
	if (ret || (h->ilim != 400 && h->ilim != 800))
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,sc-dbc-cycles", &h->debounce);
	if (ret || (h->debounce != 0 && h->debounce != 8 &&
		    h->debounce != 16 && h->debounce != 32))
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,lra-auto-res-mode", &h->auto_mode);
	if (ret || h->auto_mode < HAP_AUTO_RES_ZXD || h->auto_mode > HAP_AUTO_RES_ZXD_EOP)
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,lra-high-z", &h->high_z);
	if (ret || h->high_z > 3)
		return -EINVAL;
	ret = device_property_read_u32(dev, "qcom,lra-res-cal-period", &h->cal_period);
	if (ret || (h->cal_period != 4 && h->cal_period != 8 &&
		    h->cal_period != 16 && h->cal_period != 32))
		return -EINVAL;
	ret = device_property_count_u32(dev, "qcom,brake-pattern");
	if (ret != ARRAY_SIZE(brake))
		return -EINVAL;
	ret = device_property_read_u32_array(dev, "qcom,brake-pattern", brake, ARRAY_SIZE(brake));
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(brake); i++) {
		if (brake[i] > 3)
			return -EINVAL;
		h->brake |= brake[i] << (i * 2);
	}
	return 0;
}

static void qhap_cleanup(void *data)
{
	struct spmi_haptics *h = data;
	int ret = qhap_quiesce(h);

	if (ret)
		dev_err(h->dev, "Failed to stop haptics at teardown: %d\n", ret);
}

static int qhap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_spmi_pmic *pmic;
	struct spmi_haptics *h;
	int ret;

	pmic = qcom_pmic_get(dev);
	if (IS_ERR(pmic))
		return dev_err_probe(dev, PTR_ERR(pmic), "Missing parent PMIC\n");
	if (pmic->subtype != PMI8998_SUBTYPE)
		return -ENODEV;
	if (!(to_spmi_device(dev->parent)->usid & 1))
		return -ENODEV;
	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->dev = dev;
	h->sc_irq = -1;
	h->regmap = dev_get_regmap(dev->parent, NULL);
	if (!h->regmap)
		return -ENODEV;
	mutex_init(&h->lock);
	spin_lock_init(&h->request_lock);
	INIT_WORK(&h->work, qhap_work);
	INIT_DELAYED_WORK(&h->auto_work, qhap_auto_work);
	ret = qhap_properties(h);
	if (ret)
		return ret;
	/* Register before input/IRQ resources: their callbacks finish first. */
	ret = devm_add_action_or_reset(dev, qhap_cleanup, h);
	if (ret)
		return ret;
	ret = qhap_configure(h);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure haptics\n");
	h->sc_irq = platform_get_irq_byname(pdev, "short");
	if (h->sc_irq < 0)
		return h->sc_irq;
	ret = devm_request_threaded_irq(dev, h->sc_irq, NULL, qhap_short_irq,
					IRQF_ONESHOT, dev_name(dev), h);
	if (ret)
		return ret;
	h->input = devm_input_allocate_device(dev);
	if (!h->input)
		return -ENOMEM;
	h->input->name = "PMI8998 haptics";
	h->input->open = qhap_open;
	h->input->close = qhap_close;
	input_set_drvdata(h->input, h);
	input_set_capability(h->input, EV_FF, FF_RUMBLE);
	ret = input_ff_create_memless(h->input, NULL, qhap_play);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, h);
	return input_register_device(h->input);
}

static void qhap_shutdown(struct platform_device *pdev)
{
	struct spmi_haptics *h = platform_get_drvdata(pdev);

	guard(mutex)(&h->input->mutex);
	h->removing = true;
	qhap_cleanup(h);
}

static int qhap_suspend(struct device *dev)
{
	struct spmi_haptics *h = dev_get_drvdata(dev);

	guard(mutex)(&h->input->mutex);
	h->suspended = true;
	return qhap_quiesce(h);
}

static int qhap_resume(struct device *dev)
{
	struct spmi_haptics *h = dev_get_drvdata(dev);

	guard(mutex)(&h->input->mutex);
	guard(mutex)(&h->lock);
	h->suspended = false;
	if (h->opened && !h->removing && !h->faulted)
		qhap_gate(h, true);
	/* Old effects are not replayed across suspend. */
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(qhap_pm_ops, qhap_suspend, qhap_resume);

static const struct of_device_id qhap_match[] = {
	{ .compatible = "qcom,pmi8998-haptics" },
	{ }
};
MODULE_DEVICE_TABLE(of, qhap_match);

static struct platform_driver qhap_driver = {
	.probe = qhap_probe,
	.remove = qhap_shutdown,
	.shutdown = qhap_shutdown,
	.driver = {
		.name = "qcom-spmi-haptics",
		.of_match_table = qhap_match,
		.pm = pm_sleep_ptr(&qhap_pm_ops),
	},
};
module_platform_driver(qhap_driver);

MODULE_DESCRIPTION("PMI8998 buffer-mode LRA force feedback");
MODULE_AUTHOR("Caleb Connolly <caleb@connolly.tech>");
MODULE_LICENSE("GPL");

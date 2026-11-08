// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics FTM5 resident-firmware input driver.
 * Protocol derived from the GPLv2 STMicroelectronics fts_521 driver,
 * Copyright (C) 2016 STMicroelectronics Limited.
 *
 * No flash programming, manufacturing commands, arbitrary register interface,
 * embedded firmware or persistent calibration writes are provided.
 */
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#include "stmfts-ftm5.h"

#define STMFTM5_FIFO_READ 0x86
#define STMFTM5_READY 0x03
#define STMFTM5_ERROR 0xf3

struct stmftm5 {
	struct i2c_client *client;
	struct input_dev *input;
	struct touchscreen_properties prop;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset;
	atomic_t irq_enabled;
	bool powered;
	bool active;
	bool removing;
};

static int stmftm5_read(struct stmftm5 *ts, const u8 *cmd, u16 cmdlen,
			u8 *buffer, u16 length)
{
	struct i2c_msg msgs[] = {
		{ .addr = ts->client->addr, .len = cmdlen, .buf = (u8 *)cmd },
		{ .addr = ts->client->addr,
		  .flags = I2C_M_RD,
		  .len = length,
		  .buf = buffer },
	};
	int ret;

	ret = i2c_transfer(ts->client->adapter, msgs, ARRAY_SIZE(msgs));
	return ret < 0 ? ret : (ret == ARRAY_SIZE(msgs) ? 0 : -EIO);
}

static int stmftm5_scan(struct stmftm5 *ts, bool enable)
{
	/* ACTIVE_MULTI_TOUCH only: no key/hover/force scans or 0xff mask. */
	u8 cmd[] = { 0xa0, 0x00, enable ? 0x01 : 0x00 };
	int ret = i2c_master_send(ts->client, cmd, sizeof(cmd));

	return ret < 0 ? ret : (ret == sizeof(cmd) ? 0 : -EIO);
}

static int stmftm5_power_off(struct stmftm5 *ts)
{
	int ret;

	if (!ts->powered)
		return 0;
	gpiod_set_value_cansleep(ts->reset, 1);
	ret = regulator_bulk_disable(ARRAY_SIZE(ts->supplies), ts->supplies);
	if (!ret)
		ts->powered = false;
	else
		dev_err(&ts->client->dev, "Failed to disable supplies: %d\n",
			ret);
	return ret;
}

static void stmftm5_power_off_action(void *data)
{
	stmftm5_power_off(data);
}

static int stmftm5_power_on(struct stmftm5 *ts)
{
	const u8 fifo = STMFTM5_FIFO_READ;
	const u8 chip_read[] = { 0xfa, 0x20, 0x00, 0x00, 0x00 };
	u8 event[STMFTM5_EVENT_SIZE] = {};
	bool ready = false;
	unsigned long deadline;
	u8 chip[2];
	int ret;

	gpiod_set_value_cansleep(ts->reset, 1);
	if (!ts->powered) {
		ret = regulator_bulk_enable(ARRAY_SIZE(ts->supplies),
					    ts->supplies);
		if (ret)
			return ret;
		ts->powered = true;
	}
	fsleep(10000);
	gpiod_set_value_cansleep(ts->reset, 0);
	deadline = jiffies + msecs_to_jiffies(500);
	do {
		usleep_range(5000, 6000);
		ret = stmftm5_read(ts, &fifo, 1, event, sizeof(event));
		if (ret == -ENXIO || ret == -EREMOTEIO || ret == -EAGAIN)
			continue;
		if (ret)
			goto power_off;
		if (event[0] == STMFTM5_ERROR) {
			dev_err(&ts->client->dev,
				"Resident firmware error: %02x\n", event[1]);
			ret = -EIO;
			goto power_off;
		}
		if (event[0] == STMFTM5_READY) {
			ready = true;
			break;
		}
	} while (time_before(jiffies, deadline));
	if (!ready) {
		ret = -ETIMEDOUT;
		goto power_off;
	}
	ret = stmftm5_read(ts, chip_read, sizeof(chip_read), chip,
			   sizeof(chip));
	if (ret)
		goto power_off;
	if (chip[0] != 0x36 || chip[1] != 0x39) {
		ret = -ENODEV;
		goto power_off;
	}
	return 0;

power_off:
	stmftm5_power_off(ts);
	return ret;
}

static void stmftm5_release_all(struct stmftm5 *ts)
{
	int slot;

	for (slot = 0; slot < STMFTM5_MAX_CONTACTS; slot++) {
		input_mt_slot(ts->input, slot);
		input_mt_report_slot_inactive(ts->input);
	}
	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t stmftm5_irq(int irq, void *data)
{
	struct stmftm5 *ts = data;
	struct stmftm5_contact point;
	u8 events[STMFTM5_FIFO_DEPTH * STMFTM5_EVENT_SIZE];
	const u8 fifo = STMFTM5_FIFO_READ;
	unsigned int remaining, i;
	int ret;

	ret = stmftm5_read(ts, &fifo, 1, events, STMFTM5_EVENT_SIZE);
	if (ret)
		goto fault;
	remaining = events[7] & 0x1f;
	if (remaining) {
		ret = stmftm5_read(ts, &fifo, 1, events + STMFTM5_EVENT_SIZE,
				   remaining * STMFTM5_EVENT_SIZE);
		if (ret)
			goto fault;
	}
	for (i = 0; i <= remaining; i++) {
		u8 *event = events + i * STMFTM5_EVENT_SIZE;

		if (!event[0])
			break;
		if (event[0] == STMFTM5_ERROR || event[0] == STMFTM5_READY) {
			ret = -EIO;
			goto fault;
		}
		switch (stmftm5_decode(event, &point)) {
		case STMFTM5_RELEASE:
			input_mt_slot(ts->input, point.slot);
			input_mt_report_slot_inactive(ts->input);
			break;
		case STMFTM5_CONTACT:
			input_mt_slot(ts->input, point.slot);
			input_mt_report_slot_state(ts->input,
						   point.palm ? MT_TOOL_PALM :
								MT_TOOL_FINGER,
						   true);
			touchscreen_report_pos(ts->input, &ts->prop,
					       min(point.x, ts->prop.max_x),
					       min(point.y, ts->prop.max_y),
					       true);
			input_report_abs(ts->input, ABS_MT_PRESSURE,
					 point.pressure);
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR,
					 point.major);
			input_report_abs(ts->input, ABS_MT_TOUCH_MINOR,
					 point.minor);
			break;
		case STMFTM5_IGNORED:
			break;
		}
	}
	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
	return IRQ_HANDLED;

fault:
	/* Fail closed, rather than spin a level IRQ or flash unknown firmware. */
	if (atomic_xchg(&ts->irq_enabled, 0))
		disable_irq_nosync(irq);
	stmftm5_release_all(ts);
	dev_err_ratelimited(&ts->client->dev,
			    "Input stopped (%d); reopen or resume to retry\n",
			    ret);
	return IRQ_HANDLED;
}

/* Input core mutex serializes open/close with suspend, resume and removal. */
static int stmftm5_start(struct stmftm5 *ts)
{
	int ret;

	lockdep_assert_held(&ts->input->mutex);
	if (ts->removing)
		return -ENODEV;
	if (ts->active)
		return 0;
	ret = stmftm5_power_on(ts);
	if (ret)
		return ret;
	ret = stmftm5_scan(ts, true);
	if (ret) {
		stmftm5_power_off(ts);
		return ret;
	}
	ts->active = true;
	atomic_set(&ts->irq_enabled, 1);
	enable_irq(ts->client->irq);
	return 0;
}

static void stmftm5_stop(struct stmftm5 *ts)
{
	int ret;

	lockdep_assert_held(&ts->input->mutex);
	if (ts->active) {
		if (atomic_xchg(&ts->irq_enabled, 0))
			disable_irq(ts->client->irq);
		else
			synchronize_irq(ts->client->irq);
		ret = stmftm5_scan(ts, false);
		if (ret)
			dev_warn(&ts->client->dev, "Failed to stop scans: %d\n",
				 ret);
		ts->active = false;
		stmftm5_release_all(ts);
	}
	stmftm5_power_off(ts);
}

static int stmftm5_open(struct input_dev *input)
{
	return stmftm5_start(input_get_drvdata(input));
}

static void stmftm5_close(struct input_dev *input)
{
	stmftm5_stop(input_get_drvdata(input));
}

static int stmftm5_suspend(struct device *dev)
{
	struct stmftm5 *ts = dev_get_drvdata(dev);

	guard(mutex)(&ts->input->mutex);
	stmftm5_stop(ts);
	if (ts->powered) {
		/* A failed suspend may not receive a later resume callback. */
		if (input_device_enabled(ts->input)) {
			int ret = stmftm5_start(ts);

			if (ret)
				dev_err(dev,
					"Failed to restore input after suspend error: %d\n",
					ret);
		}
		return -EIO;
	}
	return 0;
}

static int stmftm5_resume(struct device *dev)
{
	struct stmftm5 *ts = dev_get_drvdata(dev);

	guard(mutex)(&ts->input->mutex);
	return input_device_enabled(ts->input) ? stmftm5_start(ts) : 0;
}

static void stmftm5_remove(struct i2c_client *client)
{
	struct stmftm5 *ts = i2c_get_clientdata(client);

	guard(mutex)(&ts->input->mutex);
	ts->removing = true;
	stmftm5_stop(ts);
}

static int stmftm5_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct stmftm5 *ts;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	if (client->irq <= 0)
		return -EINVAL;
	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	ts->client = client;
	ts->supplies[0].supply = "vdd";
	ts->supplies[1].supply = "avdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ts->supplies),
				      ts->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get supplies\n");
	ts->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset))
		return dev_err_probe(dev, PTR_ERR(ts->reset),
				     "Failed to get reset\n");
	ret = devm_add_action_or_reset(dev, stmftm5_power_off_action, ts);
	if (ret)
		return ret;

	ts->input = devm_input_allocate_device(dev);
	if (!ts->input)
		return -ENOMEM;
	ts->input->name = "STMicroelectronics FTM5 resident firmware";
	ts->input->id.bustype = BUS_I2C;
	ts->input->open = stmftm5_open;
	ts->input->close = stmftm5_close;
	input_set_drvdata(ts->input, ts);
	i2c_set_clientdata(client, ts);
	atomic_set(&ts->irq_enabled, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, 4095, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, 4095, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 63, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 63, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOOL_TYPE, 0, MT_TOOL_PALM, 0,
			     0);
	touchscreen_parse_properties(ts->input, true, &ts->prop);
	ret = input_mt_init_slots(ts->input, STMFTM5_MAX_CONTACTS,
				  INPUT_MT_DIRECT);
	if (ret)
		return ret;
	ret = devm_request_threaded_irq(dev, client->irq, NULL, stmftm5_irq,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					dev_name(dev), ts);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get IRQ\n");

	/* Verify existing firmware without any persistent programming. */
	ret = stmftm5_power_on(ts);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Resident FTM5 firmware unavailable\n");
	ret = stmftm5_power_off(ts);
	if (ret)
		return ret;
	return input_register_device(ts->input);
}

static DEFINE_SIMPLE_DEV_PM_OPS(stmftm5_pm_ops, stmftm5_suspend,
				stmftm5_resume);

static const struct of_device_id stmftm5_of_match[] = {
	{ .compatible = "st,stmfts-ftm5" },
	{}
};
MODULE_DEVICE_TABLE(of, stmftm5_of_match);

static struct i2c_driver stmftm5_driver = {
	.probe = stmftm5_probe,
	.remove = stmftm5_remove,
	.shutdown = stmftm5_remove,
	.driver = {
		.name = "stmfts-ftm5",
		.of_match_table = stmftm5_of_match,
		.pm = pm_sleep_ptr(&stmftm5_pm_ops),
	},
};
module_i2c_driver(stmftm5_driver);

MODULE_AUTHOR("adkaore <adkaore@gmail.com>");
MODULE_DESCRIPTION("FTM5 resident-firmware touchscreen");
MODULE_LICENSE("GPL");

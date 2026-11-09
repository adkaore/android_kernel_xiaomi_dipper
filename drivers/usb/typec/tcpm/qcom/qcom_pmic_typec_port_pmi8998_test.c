// SPDX-License-Identifier: GPL-2.0
/* Included privately so tests execute the real static callbacks, not copies. */
#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

enum handoff_event { INPUT_OFF, INPUT_ON, BOOST_ON, BOOST_OFF, CURRENT_SET };

struct handoff_test {
	struct pmic_typec tcpm;
	struct pmi8998_typec_port port;
	struct power_supply charger;
	struct regulator_init_data regulator_init;
	struct regulator_consumer_supply consumer;
	struct device *dev;
	struct kunit *test;
	enum handoff_event events[16];
	unsigned int nr_events;
	int fail_input, fail_enable, fail_disable, fail_current, current_ua;
	bool boost, input;
};

static void handoff_record(struct handoff_test *ctx, enum handoff_event event)
{
	if (ctx->nr_events < ARRAY_SIZE(ctx->events))
		ctx->events[ctx->nr_events++] = event;
	else
		KUNIT_FAIL(ctx->test, "event trace overflow");
}

static int handoff_input(struct power_supply *psy, enum power_supply_property property,
			 const union power_supply_propval *value)
{
	struct handoff_test *ctx = power_supply_get_drvdata(psy);

	if (property == POWER_SUPPLY_PROP_CURRENT_MAX) {
		handoff_record(ctx, CURRENT_SET);
		if (ctx->fail_current)
			return -EIO;
		ctx->current_ua = value->intval;
		return 0;
	}
	if (property != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;
	handoff_record(ctx, value->intval ? INPUT_ON : INPUT_OFF);
	if (ctx->fail_input)
		return -EIO;
	ctx->input = value->intval;
	return 0;
}

static int handoff_enable(struct regulator_dev *rdev)
{
	struct handoff_test *ctx = rdev_get_drvdata(rdev);

	handoff_record(ctx, BOOST_ON);
	if (ctx->fail_enable)
		return -EIO;
	ctx->boost = true;
	return 0;
}

static int handoff_disable(struct regulator_dev *rdev)
{
	struct handoff_test *ctx = rdev_get_drvdata(rdev);

	handoff_record(ctx, BOOST_OFF);
	if (ctx->fail_disable)
		return -EIO;
	ctx->boost = false;
	return 0;
}

static int handoff_enabled(struct regulator_dev *rdev)
{
	struct handoff_test *ctx = rdev_get_drvdata(rdev);

	return ctx->boost;
}

static const struct regulator_ops handoff_ops = {
	.enable = handoff_enable,
	.disable = handoff_disable,
	.is_enabled = handoff_enabled,
};

static const struct regulator_desc handoff_regulator = {
	.name = "kunit-pmi8998-vbus-provider",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &handoff_ops,
	.n_voltages = 1,
	.fixed_uV = 5000000,
};

static const struct power_supply_desc handoff_supply = {
	.name = "kunit-pmi8998-input",
	.type = POWER_SUPPLY_TYPE_USB,
	.set_property = handoff_input,
};

static void handoff_put_regulator(void *data)
{
	regulator_put(data);
}

static int handoff_init(struct kunit *test)
{
	struct handoff_test *ctx;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;
	ctx->test = test;
	ctx->dev = kunit_device_register(test, "pmi8998-handoff-test");
	if (IS_ERR(ctx->dev))
		return PTR_ERR(ctx->dev);
	ctx->consumer.supply = "kunit-pmi8998-vbus";
	ctx->regulator_init.consumer_supplies = &ctx->consumer;
	ctx->regulator_init.num_consumer_supplies = 1;
	ctx->regulator_init.constraints.valid_ops_mask = REGULATOR_CHANGE_STATUS;
	config.dev = ctx->dev;
	config.driver_data = ctx;
	config.init_data = &ctx->regulator_init;
	rdev = devm_regulator_register(ctx->dev, &handoff_regulator, &config);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);
	ctx->port.vdd_vbus = regulator_get(NULL, "kunit-pmi8998-vbus");
	if (IS_ERR(ctx->port.vdd_vbus))
		return PTR_ERR(ctx->port.vdd_vbus);
	ret = kunit_add_action_or_reset(test, handoff_put_regulator, ctx->port.vdd_vbus);
	if (ret)
		return ret;
	ctx->charger.desc = &handoff_supply;
	ctx->charger.drv_data = ctx;
	atomic_set(&ctx->charger.use_cnt, 1);
	init_rwsem(&ctx->charger.extensions_sem);
	INIT_LIST_HEAD(&ctx->charger.extensions);
	ctx->port.dev = ctx->dev;
	ctx->port.charger = &ctx->charger;
	ctx->port.max_sink_ua = 3000000;
	mutex_init(&ctx->port.lock);
	ctx->tcpm.port_priv = &ctx->port;
	ctx->input = true;
	ctx->nr_events = 0;
	return 0;
}

static void handoff_exit(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->fail_input = 0;
	ctx->fail_enable = 0;
	ctx->fail_disable = 0;
	ctx->port.stopping = false;
	ctx->nr_events = 0;
	if (ctx->port.vbus_enabled)
		pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, false);
}

static void handoff_source(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 2);
	KUNIT_EXPECT_EQ(test, ctx->events[0], INPUT_OFF);
	KUNIT_EXPECT_EQ(test, ctx->events[1], BOOST_ON);
	KUNIT_EXPECT_FALSE(test, ctx->input);
	KUNIT_EXPECT_TRUE(test, ctx->boost);
}

static void handoff_sink(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), 0);
	ctx->nr_events = 0;
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, true), 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 2);
	KUNIT_EXPECT_EQ(test, ctx->events[0], BOOST_OFF);
	KUNIT_EXPECT_EQ(test, ctx->events[1], INPUT_ON);
	KUNIT_EXPECT_TRUE(test, ctx->input);
	KUNIT_EXPECT_FALSE(test, ctx->boost);
}

static void handoff_input_failure(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->fail_input = 1;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), -EIO);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 1);
	KUNIT_EXPECT_EQ(test, ctx->events[0], INPUT_OFF);
	KUNIT_EXPECT_FALSE(test, ctx->port.vbus_enabled);
	KUNIT_EXPECT_FALSE(test, ctx->boost);
}

static void handoff_boost_failure(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->fail_enable = 1;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), -EIO);
	KUNIT_EXPECT_FALSE(test, ctx->input);
	KUNIT_EXPECT_FALSE(test, ctx->port.vbus_enabled);
	KUNIT_EXPECT_FALSE(test, ctx->boost);
}

static void handoff_disable_failure(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), 0);
	ctx->nr_events = 0;
	ctx->fail_disable = 1;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, true), -EIO);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 1);
	KUNIT_EXPECT_EQ(test, ctx->events[0], BOOST_OFF);
	KUNIT_EXPECT_FALSE(test, ctx->input);
	KUNIT_EXPECT_TRUE(test, ctx->port.vbus_enabled);
}

static void handoff_shutdown_gate(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->port.stopping = true;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false),
			-ESHUTDOWN);
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, true),
			-ESHUTDOWN);
	KUNIT_EXPECT_EQ(test, ctx->nr_events, 0);
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, true), -EINVAL);
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, false), 0);
}

static void handoff_current_bounds(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 3000, 5000), 0);
	KUNIT_EXPECT_EQ(test, ctx->current_ua, 3000000);
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 3001, 5000),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 1000, 9000),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->nr_events, 1);
}

static void handoff_source_twice(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), 0);
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 3);
	KUNIT_EXPECT_EQ(test, ctx->events[2], INPUT_OFF);
	/* One OFF must release the only regulator reference, not leave boost on. */
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, false), 0);
	KUNIT_EXPECT_FALSE(test, ctx->boost);
	KUNIT_EXPECT_FALSE(test, ctx->input);
	KUNIT_EXPECT_FALSE(test, ctx->port.vbus_enabled);
}

static void handoff_sink_without_boost(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->input = false;
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, true), 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 1);
	KUNIT_EXPECT_EQ(test, ctx->events[0], INPUT_ON);
	KUNIT_EXPECT_TRUE(test, ctx->input);
	KUNIT_EXPECT_FALSE(test, ctx->boost);
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, false), 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 2);
	KUNIT_EXPECT_EQ(test, ctx->events[1], INPUT_OFF);
	KUNIT_EXPECT_FALSE(test, ctx->input);
}

static void handoff_resume_failure(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), 0);
	ctx->nr_events = 0;
	ctx->fail_input = 1;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, true), -EIO);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 2);
	KUNIT_EXPECT_EQ(test, ctx->events[0], BOOST_OFF);
	KUNIT_EXPECT_EQ(test, ctx->events[1], INPUT_ON);
	KUNIT_EXPECT_FALSE(test, ctx->port.vbus_enabled);
	KUNIT_EXPECT_FALSE(test, ctx->boost);
	KUNIT_EXPECT_FALSE(test, ctx->input);
	ctx->fail_input = 0;
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, false, true), 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 3);
	KUNIT_EXPECT_EQ(test, ctx->events[2], INPUT_ON);
	KUNIT_EXPECT_TRUE(test, ctx->input);
}

static void handoff_boost_retry(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->fail_enable = 1;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), -EIO);
	ctx->fail_enable = 0;
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_vbus(&ctx->tcpm.tcpc, true, false), 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 4);
	KUNIT_EXPECT_EQ(test, ctx->events[2], INPUT_OFF);
	KUNIT_EXPECT_EQ(test, ctx->events[3], BOOST_ON);
	KUNIT_EXPECT_TRUE(test, ctx->boost);
	KUNIT_EXPECT_TRUE(test, ctx->port.vbus_enabled);
	KUNIT_EXPECT_FALSE(test, ctx->input);
}

static void handoff_current_failure(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->fail_current = 1;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 500, 5000),
			-EIO);
	KUNIT_EXPECT_EQ(test, ctx->current_ua, 0);
	KUNIT_ASSERT_EQ(test, ctx->nr_events, 1);
	KUNIT_EXPECT_EQ(test, ctx->events[0], CURRENT_SET);
}

static void handoff_current_reset(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 3000, 5000), 0);
	KUNIT_ASSERT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, ctx->current_ua, 0);
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 1, 0),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, U32_MAX, 5000),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->nr_events, 2);
}

static void handoff_shutdown_current(struct kunit *test)
{
	struct handoff_test *ctx = test->priv;

	ctx->port.stopping = true;
	KUNIT_EXPECT_EQ(test, pmi8998_typec_port_set_current_limit(&ctx->tcpm.tcpc, 500, 5000),
			-ESHUTDOWN);
	KUNIT_EXPECT_EQ(test, ctx->nr_events, 0);
}

static struct kunit_case handoff_cases[] = {
	KUNIT_CASE(handoff_source),
	KUNIT_CASE(handoff_sink),
	KUNIT_CASE(handoff_input_failure),
	KUNIT_CASE(handoff_boost_failure),
	KUNIT_CASE(handoff_disable_failure),
	KUNIT_CASE(handoff_shutdown_gate),
	KUNIT_CASE(handoff_current_bounds),
	KUNIT_CASE(handoff_source_twice),
	KUNIT_CASE(handoff_sink_without_boost),
	KUNIT_CASE(handoff_resume_failure),
	KUNIT_CASE(handoff_boost_retry),
	KUNIT_CASE(handoff_current_failure),
	KUNIT_CASE(handoff_current_reset),
	KUNIT_CASE(handoff_shutdown_current),
	{}
};

static struct kunit_suite handoff_suite = {
	.name = "qcom-pmi8998-input-handoff",
	.init = handoff_init,
	.exit = handoff_exit,
	.test_cases = handoff_cases,
};

kunit_test_suite(handoff_suite);

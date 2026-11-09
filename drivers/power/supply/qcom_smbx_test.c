// SPDX-License-Identifier: GPL-2.0-only
/* Private inclusion: execute real SMB2 policy through an error-injecting regmap. */
#include <kunit/device.h>
#include <kunit/test.h>

#define SMB2_TEST_BASE 0x1000
#define SMB2_TEST_SIZE 0x700

struct smb2_policy_test {
	struct smb_chip chip;
	struct power_supply supply;
	struct kunit *test;
	u8 regs[SMB2_TEST_SIZE];
	unsigned int reads, writes, status_reads, ack_after;
	unsigned int fail_read, fail_write, last_write;
};

static int smb2_test_read(void *data, unsigned int reg, unsigned int *value)
{
	struct smb2_policy_test *ctx = data;

	if (reg < SMB2_TEST_BASE || reg - SMB2_TEST_BASE >= SMB2_TEST_SIZE) {
		KUNIT_FAIL(ctx->test, "unexpected read at %#x", reg);
		return -ERANGE;
	}
	ctx->reads++;
	if (reg == SMB2_TEST_BASE + POWER_PATH_STATUS)
		ctx->status_reads++;
	if (reg == ctx->fail_read)
		return -EIO;
	*value = ctx->regs[reg - SMB2_TEST_BASE];
	if (reg == SMB2_TEST_BASE + POWER_PATH_STATUS && ctx->ack_after &&
	    ctx->status_reads >= ctx->ack_after)
		*value |= P_PATH_USBIN_SUSPEND_STS_BIT;
	return 0;
}

static int smb2_test_write(void *data, unsigned int reg, unsigned int value)
{
	struct smb2_policy_test *ctx = data;

	if (reg < SMB2_TEST_BASE || reg - SMB2_TEST_BASE >= SMB2_TEST_SIZE) {
		KUNIT_FAIL(ctx->test, "unexpected write at %#x", reg);
		return -ERANGE;
	}
	ctx->writes++;
	ctx->last_write = reg;
	if (reg == ctx->fail_write)
		return -EREMOTEIO;
	ctx->regs[reg - SMB2_TEST_BASE] = value;
	return 0;
}

static const struct regmap_bus smb2_test_bus = {
	.reg_read = smb2_test_read,
	.reg_write = smb2_test_write,
};

static const struct regmap_config smb2_test_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = SMB2_TEST_BASE + SMB2_TEST_SIZE - 1,
	.cache_type = REGCACHE_NONE,
};

static int smb2_test_init(struct kunit *test)
{
	struct smb2_policy_test *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;
	ctx->test = test;
	ctx->fail_read = U32_MAX;
	ctx->fail_write = U32_MAX;
	ctx->chip.dev = kunit_device_register(test, "pmi8998-smb2-test");
	if (IS_ERR(ctx->chip.dev))
		return PTR_ERR(ctx->chip.dev);
	ctx->chip.regmap = devm_regmap_init(ctx->chip.dev, &smb2_test_bus, ctx,
					    &smb2_test_config);
	if (IS_ERR(ctx->chip.regmap))
		return PTR_ERR(ctx->chip.regmap);
	ctx->chip.base = SMB2_TEST_BASE;
	ctx->chip.ready = true;
	ctx->chip.typec_managed = true;
	ctx->chip.input_current_limit_max_ua = 3000000;
	mutex_init(&ctx->chip.lock);
	ctx->supply.desc = &smb_psy_desc;
	ctx->supply.drv_data = &ctx->chip;
	atomic_set(&ctx->supply.use_cnt, 1);
	init_rwsem(&ctx->supply.extensions_sem);
	INIT_LIST_HEAD(&ctx->supply.extensions);
	ctx->regs[USBIN_CMD_IL] = 0x80;
	return 0;
}

static int smb2_test_online(struct smb2_policy_test *ctx, bool online)
{
	int ret;

	mutex_lock(&ctx->chip.lock);
	ret = smb_set_input_online(&ctx->chip, online);
	mutex_unlock(&ctx->chip.lock);
	return ret;
}

static void smb2_suspend_ack(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->regs[POWER_PATH_STATUS] = P_PATH_USBIN_SUSPEND_STS_BIT;
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, false), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], 0x80 | USBIN_SUSPEND_BIT);
	KUNIT_EXPECT_EQ(test, ctx->status_reads, 1);
	KUNIT_EXPECT_EQ(test, ctx->writes, 1);
	KUNIT_EXPECT_EQ(test, ctx->last_write, SMB2_TEST_BASE + USBIN_CMD_IL);
}

static void smb2_suspend_delayed_ack(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->ack_after = 3;
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, false), 0);
	KUNIT_EXPECT_EQ(test, ctx->status_reads, 3);
	KUNIT_EXPECT_EQ(test, ctx->writes, 1);
}

static void smb2_suspend_command_read_error(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->fail_read = SMB2_TEST_BASE + USBIN_CMD_IL;
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, false), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->writes, 0);
	KUNIT_EXPECT_EQ(test, ctx->status_reads, 0);
}

static void smb2_suspend_write_error(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->fail_write = SMB2_TEST_BASE + USBIN_CMD_IL;
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, false), -EREMOTEIO);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], 0x80);
	KUNIT_EXPECT_EQ(test, ctx->status_reads, 0);
}

static void smb2_suspend_status_error(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->fail_read = SMB2_TEST_BASE + POWER_PATH_STATUS;
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, false), -EIO);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], 0x80 | USBIN_SUSPEND_BIT);
	KUNIT_EXPECT_EQ(test, ctx->status_reads, 1);
}

static void smb2_suspend_timeout(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	/* No ACK is synthesized: a successful command alone must not return zero. */
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, false), -ETIMEDOUT);
	KUNIT_EXPECT_GT(test, ctx->status_reads, 1);
	KUNIT_EXPECT_EQ(test, ctx->writes, 1);
}

static void smb2_resume_no_poll(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->regs[USBIN_CMD_IL] |= USBIN_SUSPEND_BIT;
	ctx->fail_read = SMB2_TEST_BASE + POWER_PATH_STATUS;
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, true), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CMD_IL], 0x80);
	KUNIT_EXPECT_EQ(test, ctx->status_reads, 0);
}

static void smb2_unmanaged_no_io(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->chip.typec_managed = false;
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, false), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, smb2_test_online(ctx, true), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ctx->reads, 0);
	KUNIT_EXPECT_EQ(test, ctx->writes, 0);
}

static void smb2_invalid_online(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;
	union power_supply_propval value = { .intval = -1 };

	KUNIT_EXPECT_EQ(test, power_supply_set_property(&ctx->supply,
							POWER_SUPPLY_PROP_ONLINE, &value), -EINVAL);
	value.intval = 2;
	KUNIT_EXPECT_EQ(test, power_supply_set_property(&ctx->supply,
							POWER_SUPPLY_PROP_ONLINE, &value), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->reads, 0);
	KUNIT_EXPECT_EQ(test, ctx->writes, 0);
}

static void smb2_current_limits(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	mutex_lock(&ctx->chip.lock);
	KUNIT_EXPECT_EQ(test, smb_set_current_limit(&ctx->chip, -1), -EINVAL);
	KUNIT_EXPECT_EQ(test, ctx->writes, 0);
	KUNIT_EXPECT_EQ(test, smb_set_current_limit(&ctx->chip, 3025000), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], 120);
	KUNIT_EXPECT_EQ(test, ctx->last_write, SMB2_TEST_BASE + USBIN_CURRENT_LIMIT_CFG);
	KUNIT_EXPECT_EQ(test, smb_set_current_limit(&ctx->chip, 24999), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], 0);
	mutex_unlock(&ctx->chip.lock);
}

static void smb2_lower_never_raises(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;
	unsigned int code;

	mutex_lock(&ctx->chip.lock);
	for (code = 0; code <= 4; code++) {
		ctx->regs[USBIN_CURRENT_LIMIT_CFG] = code;
		KUNIT_EXPECT_EQ(test, smb_lower_input_current(&ctx->chip), 0);
		KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], code);
	}
	KUNIT_EXPECT_EQ(test, ctx->writes, 0);
	ctx->regs[USBIN_CURRENT_LIMIT_CFG] = 120;
	KUNIT_EXPECT_EQ(test, smb_lower_input_current(&ctx->chip), 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[USBIN_CURRENT_LIMIT_CFG], 4);
	mutex_unlock(&ctx->chip.lock);
}

static void smb2_not_ready(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;
	union power_supply_propval value = { .intval = 500000 };
	int ret;

	ctx->chip.ready = false;
	ret = power_supply_set_property(&ctx->supply, POWER_SUPPLY_PROP_CURRENT_MAX, &value);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
	KUNIT_EXPECT_EQ(test, power_supply_get_property(&ctx->supply,
							POWER_SUPPLY_PROP_ONLINE, &value), -EAGAIN);
	KUNIT_EXPECT_EQ(test, ctx->reads, 0);
	KUNIT_EXPECT_EQ(test, ctx->writes, 0);
}

static void smb2_disable_fallback(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->regs[CHARGING_ENABLE_CMD] = CHARGING_ENABLE_CMD_BIT;
	ctx->regs[CHGR_CFG2] = CHG_EN_SRC_BIT | 0x41;
	ctx->fail_write = SMB2_TEST_BASE + CHARGING_ENABLE_CMD;
	KUNIT_EXPECT_EQ(test, smb_disable_charging(&ctx->chip), -EREMOTEIO);
	KUNIT_EXPECT_EQ(test, ctx->writes, 2);
	KUNIT_EXPECT_EQ(test, ctx->last_write, SMB2_TEST_BASE + CHGR_CFG2);
	KUNIT_EXPECT_EQ(test, ctx->regs[CHGR_CFG2], 0x41);
}

static void smb2_disable_source_error(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	ctx->regs[CHARGING_ENABLE_CMD] = CHARGING_ENABLE_CMD_BIT;
	ctx->regs[CHGR_CFG2] = CHG_EN_SRC_BIT;
	ctx->fail_write = SMB2_TEST_BASE + CHGR_CFG2;
	KUNIT_EXPECT_EQ(test, smb_disable_charging(&ctx->chip), -EREMOTEIO);
	KUNIT_EXPECT_EQ(test, ctx->regs[CHARGING_ENABLE_CMD], 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[CHGR_CFG2], CHG_EN_SRC_BIT);
}

static void smb2_health_read_error(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;
	union power_supply_propval value = { .intval = -1 };

	ctx->fail_read = SMB2_TEST_BASE + BATTERY_CHARGER_STATUS_2;
	KUNIT_EXPECT_EQ(test, power_supply_get_property(&ctx->supply,
							POWER_SUPPLY_PROP_HEALTH, &value), -EIO);
	KUNIT_EXPECT_EQ(test, value.intval, -1);
}

static void smb2_stop_gate(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;
	union power_supply_propval value = { .intval = 500000 };
	unsigned int writes;
	int ret;

	ctx->regs[CHARGING_ENABLE_CMD] = CHARGING_ENABLE_CMD_BIT;
	ctx->regs[CHGR_CFG2] = CHG_EN_SRC_BIT;
	smb_stop(&ctx->chip);
	KUNIT_EXPECT_FALSE(test, ctx->chip.ready);
	KUNIT_EXPECT_EQ(test, ctx->regs[CHARGING_ENABLE_CMD], 0);
	KUNIT_EXPECT_EQ(test, ctx->regs[CHGR_CFG2], 0);
	writes = ctx->writes;
	ret = power_supply_set_property(&ctx->supply, POWER_SUPPLY_PROP_CURRENT_MAX, &value);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
	KUNIT_EXPECT_EQ(test, ctx->writes, writes);
}

static void smb2_sysfs_policy(struct kunit *test)
{
	struct smb2_policy_test *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, smb_property_is_writable(&ctx->supply,
						       POWER_SUPPLY_PROP_ONLINE), 0);
	KUNIT_EXPECT_EQ(test, smb_property_is_writable(&ctx->supply,
						       POWER_SUPPLY_PROP_CURRENT_MAX), 0);
	ctx->chip.typec_managed = false;
	KUNIT_EXPECT_EQ(test, smb_property_is_writable(&ctx->supply,
						       POWER_SUPPLY_PROP_CURRENT_MAX), 1);
	KUNIT_EXPECT_EQ(test, smb_property_is_writable(&ctx->supply,
						       POWER_SUPPLY_PROP_ONLINE), 0);
}

static struct kunit_case smb2_policy_cases[] = {
	KUNIT_CASE(smb2_suspend_ack),
	KUNIT_CASE(smb2_suspend_delayed_ack),
	KUNIT_CASE(smb2_suspend_command_read_error),
	KUNIT_CASE(smb2_suspend_write_error),
	KUNIT_CASE(smb2_suspend_status_error),
	KUNIT_CASE(smb2_suspend_timeout),
	KUNIT_CASE(smb2_resume_no_poll),
	KUNIT_CASE(smb2_unmanaged_no_io),
	KUNIT_CASE(smb2_invalid_online),
	KUNIT_CASE(smb2_current_limits),
	KUNIT_CASE(smb2_lower_never_raises),
	KUNIT_CASE(smb2_not_ready),
	KUNIT_CASE(smb2_disable_fallback),
	KUNIT_CASE(smb2_disable_source_error),
	KUNIT_CASE(smb2_health_read_error),
	KUNIT_CASE(smb2_stop_gate),
	KUNIT_CASE(smb2_sysfs_policy),
	{}
};

static struct kunit_suite smb2_policy_suite = {
	.name = "qcom-pmi8998-smb2-policy",
	.init = smb2_test_init,
	.test_cases = smb2_policy_cases,
};

kunit_test_suite(smb2_policy_suite);

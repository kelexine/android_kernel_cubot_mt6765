// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
static struct regulator *disp_bias_pos;
static struct regulator *disp_bias_neg;
static int regulator_inited;

int display_bias_regulator_init(void)
{
	int ret = 0;

	if (regulator_inited)
		return ret;

	/* try standard dsv_pos, then MT6370 specific variants */
	disp_bias_pos = regulator_get(NULL, "dsv_pos");
	if (IS_ERR(disp_bias_pos))
		disp_bias_pos = regulator_get(NULL, "mt6370_dsv_pos");
	if (IS_ERR(disp_bias_pos))
		disp_bias_pos = regulator_get(NULL, "mt6370_dsvp");
	if (IS_ERR(disp_bias_pos)) {
		ret = PTR_ERR(disp_bias_pos);
		pr_info("get dsv_pos fail, error: %d\n", ret);
		return ret;
	}

	/* try standard dsv_neg, then MT6370 specific variants */
	disp_bias_neg = regulator_get(NULL, "dsv_neg");
	if (IS_ERR(disp_bias_neg))
		disp_bias_neg = regulator_get(NULL, "mt6370_dsv_neg");
	if (IS_ERR(disp_bias_neg))
		disp_bias_neg = regulator_get(NULL, "mt6370_dsvn");
	if (IS_ERR(disp_bias_neg)) {
		ret = PTR_ERR(disp_bias_neg);
		pr_info("get dsv_neg fail, error: %d\n", ret);
		return ret;
	}

	regulator_inited = 1;
	return ret; /* must be 0 */

}
EXPORT_SYMBOL(display_bias_regulator_init);

int display_bias_enable_uv(unsigned int voltage_uv);

int display_bias_enable(void)
{
	/* Pin both DSV rails to exactly 5.4V matching stock MT6765 baseline */
	return display_bias_enable_uv(5400000);
}
EXPORT_SYMBOL(display_bias_enable);

/*
 * Voltage-pinned bias enable, matching the stock kernel's parameterized
 * display_bias_enable(voltage): both DSV rails are set to exactly
 * voltage_uv (min == max) before enabling pos -> 10ms -> neg.
 */
int display_bias_enable_uv(unsigned int voltage_uv)
{
	int ret = 0;
	int retval = 0;

	display_bias_regulator_init();

	ret = regulator_set_voltage(disp_bias_pos, voltage_uv, voltage_uv);
	if (ret < 0)
		pr_info("set voltage disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_set_voltage(disp_bias_neg, voltage_uv, voltage_uv);
	if (ret < 0)
		pr_info("set voltage disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_enable(disp_bias_pos);
	if (ret < 0)
		pr_info("enable regulator disp_bias_pos fail, ret = %d\n",
			ret);
	retval |= ret;

	mdelay(10);

	ret = regulator_enable(disp_bias_neg);
	if (ret < 0)
		pr_info("enable regulator disp_bias_neg fail, ret = %d\n",
			ret);
	retval |= ret;

	return retval;
}
EXPORT_SYMBOL(display_bias_enable_uv);

int display_bias_disable(void)
{
	int ret = 0;
	int retval = 0;

	display_bias_regulator_init();

	/* disable regulator: neg -> 10ms delay -> pos */
	if (disp_bias_neg && regulator_is_enabled(disp_bias_neg) > 0) {
		ret = regulator_disable(disp_bias_neg);
		if (ret < 0)
			pr_info("disable regulator disp_bias_neg fail, ret = %d\n",
				ret);
		retval |= ret;
	}

	mdelay(10);

	if (disp_bias_pos && regulator_is_enabled(disp_bias_pos) > 0) {
		ret = regulator_disable(disp_bias_pos);
		if (ret < 0)
			pr_info("disable regulator disp_bias_pos fail, ret = %d\n",
				ret);
		retval |= ret;
	}

	return retval;
}
EXPORT_SYMBOL(display_bias_disable);

#else
int display_bias_regulator_init(void)
{
	return 0;
}
EXPORT_SYMBOL(display_bias_regulator_init);

int display_bias_enable_uv(unsigned int voltage_uv);

int display_bias_enable(void)
{
	return 0;
}
EXPORT_SYMBOL(display_bias_enable);

int display_bias_enable_uv(unsigned int voltage_uv)
{
	return 0;
}
EXPORT_SYMBOL(display_bias_enable_uv);

int display_bias_disable(void)
{
	return 0;
}
EXPORT_SYMBOL(display_bias_disable);
#endif


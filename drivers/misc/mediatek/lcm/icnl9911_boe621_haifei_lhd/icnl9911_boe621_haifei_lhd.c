// SPDX-License-Identifier: GPL-2.0
/*
 * Script: icnl9911_boe621_haifei_lhd.c
 * Author: kelexine <https://github.com/kelexine>
 * Target: Cubot P50 (MT6765, Kernel 4.19.127)
 * Purpose: LCM driver for ICNL9911-based HD+ (720x1520) DSI panel (Source #2).
 *
 * Driver written from stock vmlinux.elf decompilation via Ghidra
 * (addresses: 0xffffff80087356a8 - 0xffffff8008735b08).
 */

#define LOG_TAG "LCM"

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include "lcm_drv.h"

#ifdef mdelay
#undef mdelay
#endif
#ifdef udelay
#undef udelay
#endif

/* ------------------------------------------------------------------------
 * Panel Geometry & Identification (Verified via Ghidra from stock vmlinux)
 * ---------------------------------------------------------------------- */
#define FRAME_WIDTH             (720)
#define FRAME_HEIGHT            (1520)

#define PHYSICAL_WIDTH          (67)
#define PHYSICAL_HEIGHT         (142)
#define PHYSICAL_WIDTH_UM       (67610)
#define PHYSICAL_HEIGHT_UM      (142730)
#define PANEL_DENSITY           (240)

#define LCM_ID_ICNL9911         (0x99)

#define GPIO_PANEL2_POWER_EN    (503)
#define GPIO_PANEL2_RESET       (374)

/* ------------------------------------------------------------------------
 * Bias rail (6.0V positive & negative DSV rails)
 * ---------------------------------------------------------------------- */
#define DSV_BIAS_UV             (6000000)

#define REGFLAG_DELAY           0xFFFC
#define REGFLAG_UDELAY          0xFFFB
#define REGFLAG_END_OF_TABLE    0xFFFD
#define REGFLAG_RESET_LOW       0xFFFE
#define REGFLAG_RESET_HIGH      0xFFFF

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};





/* ------------------------------------------------------------------------
 * MediaTek LCM util glue
 * ---------------------------------------------------------------------- */
static struct LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)        (lcm_util.set_reset_pin((v)))
#define UDELAY(n)               (lcm_util.udelay((n)))
#define MDELAY(n)               (lcm_util.mdelay((n)))
#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	(lcm_util.dsi_set_cmdq_V22((cmdq), (cmd), (count), (ppara), (force_update)))
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	(lcm_util.dsi_set_cmdq_V2((cmd), (count), (ppara), (force_update)))
#define dsi_set_cmdq(pdata, queue_size, force_update) \
	(lcm_util.dsi_set_cmdq((pdata), (queue_size), (force_update)))
#define read_reg_v2(cmd, buffer, buffer_size) \
	(lcm_util.dsi_dcs_read_lcm_reg_v2((cmd), (buffer), (buffer_size)))

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	params->physical_width = PHYSICAL_WIDTH;
	params->physical_height = PHYSICAL_HEIGHT;
	params->physical_width_um = PHYSICAL_WIDTH_UM;
	params->physical_height_um = PHYSICAL_HEIGHT_UM;
	params->density = PANEL_DENSITY;

	params->dsi.mode = BURST_VDO_MODE;
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;
	params->dsi.packet_size = 256;

	params->dsi.vertical_sync_active = 4;
	params->dsi.vertical_backporch = 12;
	params->dsi.vertical_frontporch = 150;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 4;
	params->dsi.horizontal_backporch = 48;
	params->dsi.horizontal_frontporch = 48;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;

	params->dsi.PLL_CLOCK = 261;
	params->dsi.ssc_disable = 1;

	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;
}

/* ------------------------------------------------------------------------
 * 38-entry register initialization table recovered from stock vmlinux
 * ---------------------------------------------------------------------- */
static struct LCM_setting_table lcm_initialization_setting[] = {
	{0xF0, 2, {0x5A, 0x59}},
	{0xF1, 2, {0xA5, 0xA6}},
	{0xB0, 30, {0x87, 0x86, 0x85, 0x84, 0x02, 0x03, 0x04, 0x05, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x0F, 0x05, 0x04, 0x03, 0x02, 0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00}},
	{0xB1, 29, {0x53, 0x43, 0x85, 0x80, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x04, 0x08, 0x54, 0x00, 0x00, 0x00, 0x44, 0x40, 0x02, 0x01, 0x40, 0x02, 0x01, 0x40, 0x02, 0x01, 0x40, 0x02, 0x01}},
	{0xB2, 17, {0x54, 0xC4, 0x82, 0x05, 0x40, 0x02, 0x01, 0x40, 0x02, 0x01, 0x05, 0x05, 0x54, 0x0C, 0x0C, 0x0D, 0x0B}},
	{0xB3, 31, {0x02, 0x0C, 0x06, 0x0C, 0x06, 0x26, 0x26, 0x91, 0xA2, 0x33, 0x44, 0x00, 0x26, 0x00, 0x18, 0x01, 0x02, 0x08, 0x20, 0x30, 0x08, 0x09, 0x44, 0x20, 0x40, 0x20, 0x40, 0x08, 0x09, 0x22, 0x33}},
	{0xB4, 28, {0x00, 0x23, 0x1D, 0x06, 0x04, 0x00, 0x10, 0x12, 0x0C, 0x0E, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFC, 0x60, 0x30, 0x00}},
	{0xB5, 28, {0x00, 0x23, 0x1D, 0x07, 0x05, 0x00, 0x11, 0x13, 0x0D, 0x0F, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFC, 0x60, 0x30, 0x00}},
	{0xB8, 24, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xBB, 13, {0x01, 0x05, 0x09, 0x11, 0x0D, 0x19, 0x1D, 0x55, 0x25, 0x69, 0x00, 0x21, 0x25}},
	{0xBC, 14, {0x00, 0x00, 0x00, 0x00, 0x02, 0x20, 0xFF, 0x00, 0x03, 0x33, 0x01, 0x73, 0x33, 0x00}},
	{0xBD, 10, {0xE9, 0x02, 0x4E, 0xCF, 0x72, 0xA4, 0x08, 0x44, 0xAE, 0x15}},
	{0xBE, 10, {0x72, 0x72, 0x46, 0x5A, 0x0C, 0x77, 0x43, 0x07, 0x0E, 0x0E}},
	{0xBF, 8, {0x07, 0x25, 0x07, 0x25, 0x7F, 0x00, 0x11, 0x04}},
	{0xFA, 3, {0x45, 0x93, 0x01}},
	{0xF6, 1, {0x3F}},
	{0xC0, 9, {0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0x00}},
	{0xC1, 19, {0xC0, 0x0C, 0x20, 0x96, 0x04, 0x30, 0x30, 0x04, 0x2A, 0xF0, 0x35, 0x00, 0x07, 0xCF, 0xFF, 0xFF, 0x9E, 0x01, 0xC0}},
	{0xC2, 1, {0x00}},
	{0xC3, 11, {0x06, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x81, 0x01, 0x00, 0x00}},
	{0xC4, 10, {0x84, 0x01, 0x2B, 0x41, 0x00, 0x3C, 0x00, 0x03, 0x03, 0x2E}},
	{0xC5, 11, {0x03, 0x1C, 0xB8, 0xB8, 0x30, 0x10, 0x42, 0x44, 0x08, 0x09, 0x14}},
	{0xC6, 10, {0x87, 0x9B, 0x2A, 0x29, 0x29, 0x33, 0x64, 0x34, 0x08, 0x04}},
	{0xC7, 22, {0xF7, 0xDD, 0xC8, 0xB6, 0x93, 0x77, 0x48, 0x99, 0x5C, 0x28, 0xF3, 0xB5, 0x03, 0xCE, 0xAC, 0x7E, 0x63, 0x3E, 0x1A, 0x7F, 0xE4, 0x00}},
	{0xC8, 22, {0xF7, 0xDD, 0xC8, 0xB6, 0x93, 0x77, 0x48, 0x99, 0x5C, 0x28, 0xF3, 0xB5, 0x03, 0xCE, 0xAC, 0x7E, 0x63, 0x3E, 0x1A, 0x7F, 0xE4, 0x00}},
	{0xCB, 1, {0x00}},
	{0xD0, 5, {0x80, 0x0D, 0xFF, 0x0F, 0x61}},
	{0xD2, 1, {0x42}},
	{0xFE, 4, {0xFF, 0xFF, 0xFF, 0x40}},
	{0xF1, 2, {0x5A, 0x59}},
	{0xF0, 2, {0xA5, 0xA6}},
	{0x35, 1, {0x00}},
	{0x11, 1, {0x00}}, /* DCS Sleep Out */
	{REGFLAG_DELAY, 120, {}},
	{0x29, 1, {0x00}}, /* DCS Display ON */
	{REGFLAG_DELAY, 10, { }},
	{0x26, 1, {0x01}},
	{REGFLAG_DELAY, 5, { }},
};

/* ------------------------------------------------------------------------
 * 8-entry register suspend table recovered from stock vmlinux (0xffffff8009b8aad0)
 * ---------------------------------------------------------------------- */
static struct LCM_setting_table lcm_suspend_setting[] = {
	{ REGFLAG_DELAY, 20, {} },
	{ 0x26, 1, { 0x08 } },
	{ REGFLAG_DELAY, 5, {} },
	{ 0x28, 0, {} },
	{ REGFLAG_DELAY, 20, {} },
	{ 0x10, 0, {} },
	{ REGFLAG_DELAY, 120, {} },
	{ REGFLAG_END_OF_TABLE, 0, {} }
};

static void push_table(struct LCM_setting_table *table, unsigned int count,
			unsigned char force_update)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		unsigned int cmd = table[i].cmd;

		switch (cmd) {
		case REGFLAG_END_OF_TABLE:
			continue;
		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			continue;
		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			continue;
		default:
			dsi_set_cmdq_V22(NULL, cmd, table[i].count, table[i].para_list, force_update);
		}
	}
}

/* ------------------------------------------------------------------------
 * Power & Lifecycle sequencing (from stock vmlinux)
 * ---------------------------------------------------------------------- */
static void lcm_init_power(void)
{
	pr_info("[LCM] icnl9911_boe621: lcm_init_power (enabling bias)\n");
	SET_RESET_PIN(0);
	MDELAY(30);
	display_bias_enable_uv(DSV_BIAS_UV);
}

static void lcm_suspend_power(void)
{
	pr_info("[LCM] icnl9911_boe621: lcm_suspend_power (disabling bias)\n");
	display_bias_disable();
}

static void lcm_resume_power(void)
{
	pr_info("[LCM] icnl9911_boe621: lcm_resume_power (enabling bias)\n");
	SET_RESET_PIN(0);
	MDELAY(30);
	display_bias_enable_uv(DSV_BIAS_UV);
	MDELAY(30);
}

static void lcm_init(void)
{
	pr_info("[LCM] icnl9911_boe621: lcm_init start\n");
	SET_RESET_PIN(1);
	MDELAY(20);
	SET_RESET_PIN(0);
	MDELAY(20);
	SET_RESET_PIN(1);
	MDELAY(150);

	push_table(lcm_initialization_setting,
		   sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table),
		   1);
	pr_info("[LCM] icnl9911_boe621: lcm_init complete\n");
}

static void lcm_suspend(void)
{
	pr_info("[LCM] icnl9911_boe621: lcm_suspend\n");
	push_table(lcm_suspend_setting,
		   sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table),
		   1);
	MDELAY(10);
}

/*
 * Panel F6 register override from the bootloader cmdline
 * (androidboot.Temp_F6=0xXY), applied to init table entry 15 before
 * every re-init, mirroring the stock driver. Default 0x3F.
 */
static unsigned char panel_f6_reg_val = 0x3F;

static void lcm_resume(void)
{
	const char *p = strstr(saved_command_line, "androidboot.Temp_F6=0x");

	if (p) {
		int hi = hex_to_bin(p[22]);
		int lo = hex_to_bin(p[23]);

		if (hi >= 0 && lo >= 0) {
			panel_f6_reg_val = (unsigned char)((hi << 4) | lo);
			lcm_initialization_setting[15].para_list[0] =
				panel_f6_reg_val;
		}
	}
	pr_info("[LCM] icnl9911_boe621: lcm_resume (F6=0x%02X)\n",
		panel_f6_reg_val);
	lcm_init();
}

/* ------------------------------------------------------------------------
 * ID Compare: Send 0x00023700, read reg 0xA1 == 0x99
 * ---------------------------------------------------------------------- */
static unsigned int lcm_compare_id(void)
{
	unsigned char buffer[4] = { 0 };
	unsigned int cmd_array[4] = { 0x00023700 };

	SET_RESET_PIN(1);
	MDELAY(20);
	SET_RESET_PIN(0);
	MDELAY(20);
	SET_RESET_PIN(1);
	MDELAY(120);

	dsi_set_cmdq(cmd_array, 1, 1);
	read_reg_v2(0xA1, buffer, 1);

	return (buffer[0] == LCM_ID_ICNL9911) ? 1 : 0;
}

struct LCM_DRIVER icnl9911_boe621_haifei_lhd_lcm_drv = {
	.name           = "icnl9911_boe621_haifei_lhd",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params     = lcm_get_params,
	.init           = lcm_init,
	.suspend        = lcm_suspend,
	.resume         = lcm_resume,
	.compare_id     = lcm_compare_id,
	.init_power     = lcm_init_power,
	.suspend_power  = lcm_suspend_power,
	.resume_power   = lcm_resume_power,
};

// SPDX-License-Identifier: GPL-2.0
/*
 * Script: ilitek_i2c.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-08-21
 * Purpose: I2C bus interface driver for ILITEK TDDI Touch IC (Cubot P50 / MT6765).
 *
 * Implemented and verified against stock kernel decompilation via Ghidra
 * (addresses: 0xffffff8008a3ed78 - 0xffffff8008a3f2cc).
 */

#include "ilitek.h"
#include <linux/i2c.h>

struct touch_bus_info {
	struct i2c_driver bus_driver;
	struct ilitek_hwif_info *hwif;
};

struct ilitek_tddi_dev *idev;

int ilitek_i2c_read(void *buf, int len)
{
	int ret = 0;
	struct i2c_msg msg;

	if (!idev || !idev->i2c) {
		ipio_err("Invalid idev or i2c client\n");
		return -EINVAL;
	}

	if (len <= 0) {
		ipio_err("Invalid read length: %d\n", len);
		return -EINVAL;
	}

	msg.addr = idev->i2c->addr;
	msg.flags = I2C_M_RD;
	msg.len = len;
	msg.buf = (u8 *)buf;

	ret = i2c_transfer(idev->i2c->adapter, &msg, 1);
	if (ret != 1) {
		if (atomic_read(&idev->tp_reset) == 1)
			return 0;
		if (atomic_read(&idev->ice_stat) != 1)
			ipio_err("i2c read error, ret = %d (addr=0x%02x, len=%d)\n",
				 ret, idev->i2c->addr, len);
		return (ret < 0) ? ret : -EIO;
	}

	return len;
}

int ilitek_i2c_write(void *buf, int len)
{
	int ret = 0;
	struct i2c_msg msg;
	u8 *tx_buf = (u8 *)buf;
	u8 *alloc_buf = NULL;
	int write_len = len;

	if (!idev || !idev->i2c) {
		ipio_err("Invalid idev or i2c client\n");
		return -EINVAL;
	}

	if (len <= 0) {
		ipio_err("Invalid write length: %d\n", len);
		return -EINVAL;
	}

	/* Stock behavior: if core_ver >= 1.4.1, MP test mode, and CDC init packet (0xF1), append checksum */
	if (idev->chip && (idev->chip->core_ver >= CORE_VER_1410) &&
	    (tx_buf[0] == P5_X_SET_CDC_INIT) &&
	    (idev->actual_tp_mode == P5_X_FW_TEST_MODE)) {
		alloc_buf = kmalloc(len + 1, GFP_KERNEL);
		if (!alloc_buf) {
			ipio_err("Failed to allocate mp cdc checksum buffer\n");
			return -ENOMEM;
		}
		memcpy(alloc_buf, tx_buf, len);
		alloc_buf[len] = ilitek_calc_packet_checksum(tx_buf, len);
		tx_buf = alloc_buf;
		write_len = len + 1;
		ilitek_dump_data(tx_buf, 8, write_len, 0, "mp cdc cmd with checksum");
	}

	msg.addr = idev->i2c->addr;
	msg.flags = 0;
	msg.len = write_len;
	msg.buf = tx_buf;

	ret = i2c_transfer(idev->i2c->adapter, &msg, 1);

	if (alloc_buf)
		kfree(alloc_buf);

	if (ret != 1) {
		if (atomic_read(&idev->tp_reset) == 1)
			return 0;
		if (atomic_read(&idev->ice_stat) != 1)
			ipio_err("i2c write error, ret = %d (addr=0x%02x, len=%d)\n",
				 ret, idev->i2c->addr, len);
		return (ret < 0) ? ret : -EIO;
	}

	return 0;
}

static int ilitek_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct touch_bus_info *info;
	int ret;

	if (tpd_load_status || idev != NULL) {
		ipio_info("Touch driver already initialized, skipping secondary node (addr 0x%02x)\n", client->addr);
		return -EBUSY;
	}

	ipio_info("ilitek i2c probe, addr = 0x%02x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		ipio_err("I2C functionality check failed\n");
		return -ENODEV;
	}

	if (client->addr != TDDI_I2C_ADDR) {
		ipio_info("Update client addr from 0x%02x to 0x%02x\n", client->addr, TDDI_I2C_ADDR);
		client->addr = TDDI_I2C_ADDR;
	}

	idev = devm_kzalloc(&client->dev, sizeof(struct ilitek_tddi_dev), GFP_KERNEL);
	if (!idev) {
		ipio_err("Failed to allocate idev memory\n");
		return -ENOMEM;
	}

	idev->tr_buf = kmalloc(TR_BUF_SIZE, GFP_KERNEL);
	if (!idev->tr_buf) {
		ipio_err("Failed to allocate touch report buffer\n");
		return -ENOMEM;
	}

	idev->gcoord = kmalloc(sizeof(struct gesture_coordinate), GFP_KERNEL);
	if (!idev->gcoord) {
		ipio_err("Failed to allocate gesture coordinate buffer\n");
		kfree(idev->tr_buf);
		return -ENOMEM;
	}

	info = container_of(to_i2c_driver(client->dev.driver), struct touch_bus_info, bus_driver);

	idev->i2c = client;
	idev->spi = NULL;
	idev->dev = &client->dev;
	idev->hwif = info->hwif;
	idev->phys = "I2C";

	idev->write = ilitek_i2c_write;
	idev->read = ilitek_i2c_read;
	idev->wrapper = ilitek_wrapper;

	idev->actual_tp_mode = P5_X_FW_AP_MODE;
	idev->tp_data_len = P5_X_DEMO_MODE_PACKET_LEN;
	idev->tp_data_mode = AP_MODE;
	idev->reset = TP_HW_RST_ONLY;
	idev->rst_edge_delay = 100;
	idev->fw_open = FILP_OPEN;
	idev->fw_upgrade_mode = UPGRADE_FLASH;
	idev->mp_move_code = ilitek_tddi_move_mp_code_flash;
	idev->gesture_move_code = ilitek_tddi_move_gesture_code_flash;
	idev->esd_recover = ilitek_tddi_wq_esd_i2c_check;
	idev->ges_recover = ilitek_tddi_touch_esd_gesture_flash;
	idev->gesture_mode = DATA_FORMAT_GESTURE_INFO;
	idev->gesture_demo_ctrl = DISABLE;
	idev->wtd_ctrl = ON;
	idev->report = ENABLE;
	idev->netlink = DISABLE;
	idev->dnp = DISABLE;
	idev->mp_retry = DISABLE;
	idev->irq_tirgger_type = IRQF_TRIGGER_FALLING;
	idev->info_from_hex = ENABLE;
	idev->wait_int_timeout = AP_INT_TIMEOUT;

#if ENABLE_GESTURE
	idev->gesture = DISABLE;
	idev->ges_sym.double_tap = DOUBLE_TAP;
	idev->ges_sym.alphabet_line_2_top = ALPHABET_LINE_2_TOP;
	idev->ges_sym.alphabet_line_2_bottom = ALPHABET_LINE_2_BOTTOM;
	idev->ges_sym.alphabet_line_2_left = ALPHABET_LINE_2_LEFT;
	idev->ges_sym.alphabet_line_2_right = ALPHABET_LINE_2_RIGHT;
	idev->ges_sym.alphabet_m = ALPHABET_M;
	idev->ges_sym.alphabet_w = ALPHABET_W;
	idev->ges_sym.alphabet_c = ALPHABET_C;
	idev->ges_sym.alphabet_E = ALPHABET_E;
	idev->ges_sym.alphabet_V = ALPHABET_V;
	idev->ges_sym.alphabet_O = ALPHABET_O;
	idev->ges_sym.alphabet_S = ALPHABET_S;
	idev->ges_sym.alphabet_Z = ALPHABET_Z;
	idev->ges_sym.alphabet_V_down = ALPHABET_V_DOWN;
	idev->ges_sym.alphabet_V_left = ALPHABET_V_LEFT;
	idev->ges_sym.alphabet_V_right = ALPHABET_V_RIGHT;
	idev->ges_sym.alphabet_two_line_2_bottom = ALPHABET_TWO_LINE_2_BOTTOM;
	idev->ges_sym.alphabet_F = ALPHABET_F;
	idev->ges_sym.alphabet_AT = ALPHABET_AT;
#endif

	i2c_set_clientdata(client, idev);

	ret = info->hwif->plat_probe();
	if (ret < 0) {
		kfree(idev->gcoord);
		kfree(idev->tr_buf);
		i2c_set_clientdata(client, NULL);
		idev = NULL;
	}

	return ret;
}

static int ilitek_i2c_remove(struct i2c_client *client)
{
	ipio_info("ilitek i2c remove\n");
	if (idev && idev->hwif && idev->hwif->plat_remove)
		idev->hwif->plat_remove();
	return 0;
}

static int ilitek_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	strcpy(info->type, TDDI_DEV_ID);
	return 0;
}

static const struct i2c_device_id tp_i2c_id[] = {
	{ TDDI_DEV_ID, 0 },
	{ "tpd", 0 },
	{ }
};

static const unsigned short force[] = {
	0, TDDI_I2C_ADDR, I2C_CLIENT_END, I2C_CLIENT_END
};
static const unsigned short *const forces[] = { force, NULL };

int ilitek_tddi_interface_dev_init(struct ilitek_hwif_info *hwif)
{
	int ret = 0;
	struct touch_bus_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ipio_err("Failed to allocate touch_bus_info\n");
		return -ENOMEM;
	}

	if (hwif->bus_type != BUS_I2C) {
		ipio_err("Invalid bus type: %d (expected BUS_I2C)\n", hwif->bus_type);
		kfree(info);
		return -EINVAL;
	}

	hwif->info = info;
	info->hwif = hwif;

	info->bus_driver.driver.name = hwif->name;
	info->bus_driver.driver.owner = hwif->owner;
	info->bus_driver.driver.of_match_table = hwif->of_match_table;
	info->bus_driver.driver.pm = hwif->pm;

	info->bus_driver.probe = ilitek_i2c_probe;
	info->bus_driver.remove = ilitek_i2c_remove;
	info->bus_driver.detect = ilitek_i2c_detect;
	info->bus_driver.id_table = tp_i2c_id;
	info->bus_driver.address_list = (const unsigned short *)forces;

	ret = i2c_add_driver(&info->bus_driver);
	if (ret != 0) {
		ipio_err("i2c_add_driver failed: %d\n", ret);
		kfree(info);
		return ret;
	}

	ipio_info("ilitek i2c driver registered successfully\n");
	return 0;
}

void ilitek_tddi_interface_dev_exit(struct ilitek_tddi_dev *dev)
{
	struct touch_bus_info *info;

	if (!dev || !dev->hwif || !dev->hwif->info)
		return;

	info = (struct touch_bus_info *)dev->hwif->info;
	ipio_info("Unregistering ilitek i2c driver\n");

	i2c_del_driver(&info->bus_driver);
	kfree(info);
	dev->hwif->info = NULL;
}

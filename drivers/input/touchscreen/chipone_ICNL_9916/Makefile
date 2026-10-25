ccflags-y += -I$(srctree)/drivers/input/touchscreen/chipone_ICNL_9916/
ccflags-y += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function

obj-$(CONFIG_TOUCHSCREEN_CHIPONE) += cts_i2c_driver.o \
	cts_core.o \
	cts_sfctrlv2.o \
	cts_spi_flash.o \
	cts_firmware.o \
	cts_test.o \
	cts_charger_detect.o \
	cts_earjack_detect.o \
	cts_hostcomm.o \
	cts_tcs.o \
	cts_platform.o \
	cts_tool.o \
	cts_sysfs.o \
	cts_strerror.o \
	cts_oem.o

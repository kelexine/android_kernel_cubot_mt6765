#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#

ifneq ($(KERNELRELEASE),)
    subdir-ccflags-y += -I$(srctree)/
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/clkbuf/src
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/clkbuf_v1/$(MTK_PLATFORM)
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
    subdir-ccflags-y += -I$(srctree)/drivers/mmc/core
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eccci/$(MTK_PLATFORM)
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eccci/
    subdir-ccflags-y += -I$(srctree)/drivers/clk/mediatek/
    subdir-ccflags-y += -I$(srctree)/drivers/pinctrl/mediatek/

    # Built-in kernel symbol adapter (exports connectivity_export_* / wmt_export_* to .ko modules)
    MODULE_NAME := connadp
    obj-$(CONFIG_MTK_COMBO) += $(MODULE_NAME).o

    $(MODULE_NAME)-objs += common/connectivity_build_in_adapter.o
    $(MODULE_NAME)-objs += common/wmt_build_in_adapter.o

    ifeq ($(CONFIG_MTK_COMBO), y)
        ccflags-y += -D CFG_CONNADP_BUILD_IN
    endif
    subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/connectivity/common

    # MediaTek In-Tree Connectivity Subsystems
    obj-$(CONFIG_MTK_COMBO) += common/
    obj-$(CONFIG_MTK_COMBO) += bt/mt66xx/legacy/
    obj-$(CONFIG_MTK_GPS_SUPPORT) += gps/
    obj-$(CONFIG_MTK_COMBO_WIFI) += wlan/adaptor/
    obj-$(CONFIG_MTK_COMBO_WIFI) += wlan/core/gen4m/
else
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    PWD  := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

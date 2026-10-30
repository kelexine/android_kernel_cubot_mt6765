###############################################################################
# Necessary Check

ifneq ($(KERNEL_OUT),)
    ccflags-y += -imacros $(KERNEL_OUT)/include/generated/autoconf.h
else
    ccflags-y += -imacros $(objtree)/include/generated/autoconf.h
endif

ifndef TOP
    TOP := $(srctree)/..
endif
CONN_DIR ?= $(srctree)/drivers/misc/mediatek/connectivity
ifeq ($(wildcard $(CONN_DIR)/common),)
    ifeq ($(wildcard $(TOP)/vendor/mediatek/kernel_modules),)
        TOP := $(abspath $(src)/../../../../..)
    endif
    CONN_DIR := $(TOP)/vendor/mediatek/kernel_modules/connectivity
endif
ifeq ($(KBUILD_EXTRA_SYMBOLS),)
    KBUILD_EXTRA_SYMBOLS := $(abspath $(CONN_DIR)/common/Module.symvers)
endif
ccflags-y += -Wno-error -Wno-error=strict-prototypes -Wno-strict-prototypes

# Force build fail on modpost warning
KBUILD_MODPOST_FAIL_ON_WARNINGS := y

ccflags-y += \
    -I$(srctree)/drivers/misc/mediatek/include/mt-plat \
    -I$(CONN_DIR)/common/common_main/include \
    -I$(CONN_DIR)/common/common_main/linux/include

ifeq ($(ADAPTOR_OPTS),CONNAC2X2_SOC3_0)
ccflags-y += -I$(CONN_DIR)/conninfra/include
ccflags-y += -I$(CONN_DIR)/conninfra/debug_utility
ccflags-y += -I$(CONN_DIR)/conninfra/debug_utility/include
ccflags-y += -I$(CONN_DIR)/conninfra/debug_utility/connsyslog
ccflags-y += -I$(CONN_DIR)/conninfra/debug_utility/coredump
endif

ifneq ($(CONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH),)
ccflags-y += -DCONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH
ccflags-y += -I$(CONN_DIR)/common/debug_utility
endif

ifeq ($(CONFIG_MTK_CONN_LTE_IDC_SUPPORT),y)
    ccflags-y += -DWMT_IDC_SUPPORT=1
else
    ccflags-y += -DWMT_IDC_SUPPORT=0
endif

ifeq ($(ADAPTOR_OPTS),CONNAC2X2_SOC3_0)
ccflags-y += -DCFG_ANDORID_CONNINFRA_SUPPORT=1
ccflags-y += -DCFG_ANDORID_CONNINFRA_COREDUMP_SUPPORT=1
else
ccflags-y += -DCFG_ANDORID_CONNINFRA_SUPPORT=0
ccflags-y += -DCFG_ANDORID_CONNINFRA_COREDUMP_SUPPORT=0
endif

ccflags-y += -D MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT

ccflags-y += -D CREATE_NODE_DYNAMIC=1

MODULE_NAME := wmt_chrdev_wifi
ifeq ($(CONFIG_WLAN_DRV_BUILD_IN),y)
$(warning $(MODULE_NAME) build-in boot.img)
obj-y += $(MODULE_NAME).o
else
$(warning $(MODULE_NAME) is kernel module)
obj-m += $(MODULE_NAME).o
endif

# Wi-Fi character device driver
$(MODULE_NAME)-objs += wmt_cdev_wifi.o
ifneq ($(CONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH),)
$(MODULE_NAME)-objs += fw_log_wifi.o
endif
ifeq ($(ADAPTOR_OPTS),CONNAC2X2_SOC3_0)
$(MODULE_NAME)-objs += wifi_pwr_on.o
endif

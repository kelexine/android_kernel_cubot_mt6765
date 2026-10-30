LOCAL_PATH := $(call my-dir)

# kernel version check
KERNEL_VER_RAW := $(if $(findstring -,$(LINUX_KERNEL_VERSION)),$(word 2,$(subst -, ,$(LINUX_KERNEL_VERSION))),$(LINUX_KERNEL_VERSION))
MAJOR_VERSION := $(or $(shell echo "$(KERNEL_VER_RAW)" | cut -f1 -d. 2>/dev/null | grep -E '^[0-9]+$$'),0)
MINOR_VERSION := $(or $(shell echo "$(KERNEL_VER_RAW)" | cut -f2 -d. 2>/dev/null | grep -E '^[0-9]+$$'),0)
VERSION_CHECK := $(shell ( [ "$(MAJOR_VERSION)" -gt 4 ] || ( [ "$(MAJOR_VERSION)" -eq 4 ] && [ "$(MINOR_VERSION)" -ge 19 ] ) ) 2>/dev/null && echo true)

ifeq ($(VERSION_CHECK), true)

include $(CLEAR_VARS)
LOCAL_MODULE := met.ko
LOCAL_STRIP_MODULE := true

include $(MTK_KERNEL_MODULE)

endif # Kernel version >= 4.19

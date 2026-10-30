#
# Copyright (C) 2018 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

PPM_ROOT_DIR ?= $(srctree)/drivers/misc/mediatek/base/power/ppm_v3
MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))

ccflags-y +=	\
	-I$(src) \
	-I$(PPM_ROOT_DIR)/inc \
	-I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include \
	-I$(srctree)/drivers/misc/mediatek/base/power/$(MTK_PLATFORM)/	\
	-I$(srctree)/drivers/misc/mediatek/thermal/$(MTK_PLATFORM)/inc/	\
	-I$(srctree)/drivers/misc/mediatek/base/power/include \
	-I$(srctree)/drivers/gpu/mediatek/gpufreq/include/

# platform dependent functions
obj-y += mtk_ppm_platform.o
# power data related
obj-y += mtk_ppm_power_data.o
# limit calculation algorithm
obj-y += mtk_ppm_cobra_algo.o

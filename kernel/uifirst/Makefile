# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for UIFIRST UX scheduler and lock priority inheritance
#

# Core UX runqueue/WALT helpers referenced unconditionally by kernel/sched/
obj-y += uifirst_sched_common.o

# Mutex and rwsem UX priority inheritance hooks
obj-$(CONFIG_UIFIRST) += uifirst_sched_mutex.o uifirst_sched_rwsem.o

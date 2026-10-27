#!/usr/bin/env bash
# Author: kelexine <https://github.com/kelexine>
#
# run_tests.sh - tests for the Chipone/Ilitek panel gate and node binding.
#
#   1. host unit tests for mtk_lcm_gate.h (gcc only)
#   2. static policy checks on the Chipone binding (match tables, address check)
#   3. optional kernel object build, when KERNEL_O (an O= dir prepared with
#      `make prepare`) and CROSS_COMPILE are provided
#
# Usage:   tests/run_tests.sh
# Env:     KERNEL_O       prepared kernel output dir (enables step 3)
#          CROSS_COMPILE  e.g. aarch64-linux-gnu-  (step 3, default shown)
#          CC             host compiler for step 1 (default: gcc)
# Exit:    0 all passed, 1 failure

set -uo pipefail

readonly HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly TS_DIR="$(dirname "${HERE}")"
readonly CTS="${TS_DIR}/chipone_ICNL_9916"
readonly CC="${CC:-gcc}"
readonly WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

FAIL=0
ok()   { printf '  \033[32mPASS\033[0m %s\n' "$*"; }
bad()  { printf '  \033[31mFAIL\033[0m %s\n' "$*" >&2; FAIL=$((FAIL + 1)); }

echo "== 1. host unit tests (mtk_lcm_gate.h)"
if "${CC}" -std=gnu11 -Wall -Wextra -Werror -O2 \
        -o "${WORK}/gate_test" "${HERE}/mtk_lcm_gate_test.c" 2>"${WORK}/cc.log"; then
    ok "compiles warning-free"
    if "${WORK}/gate_test"; then ok "unit tests pass"; else bad "unit tests failed"; fi
else
    cat "${WORK}/cc.log" >&2
    bad "unit test build failed"
fi

echo "== 2. static policy checks (Chipone binding)"
drv="${CTS}/cts_i2c_driver.c"
cfg="${CTS}/cts_plat_qcom_config.h"

# Only the 0x48 node (cap_touch3) may match; cap_touch2 (0x38) must not.
if grep -q '"mediatek,cap_touch3"' "${drv}"; then ok "of_match claims cap_touch3"; else bad "of_match lost cap_touch3"; fi
if grep -q '"mediatek,cap_touch2"' "${drv}"; then bad "of_match still claims cap_touch2"; else ok "of_match does not claim cap_touch2"; fi
if grep -q '{ "cap_touch2", 0 }' "${drv}"; then bad "i2c_device_id still lists cap_touch2"; else ok "i2c_device_id does not list cap_touch2"; fi

# The normal-mode address check must not be compiled out on MTK.
if awk '/#if !defined\(CONFIG_MTK_PLATFORM\)/{f=1} f&&/CTS_DEV_NORMAL_MODE_I2CADDR/{print "x"; exit} /#endif/{f=0}' "${drv}" | grep -q x; then
    bad "address check is compiled out on CONFIG_MTK_PLATFORM"
else
    ok "address check is active on MTK"
fi
grep -q 'client->addr != CTS_DEV_NORMAL_MODE_I2CADDR' "${drv}" && ok "address check present" || bad "address check missing"

# The panel gate must be enabled and wired into probe.
grep -q '^#define CTS_MTK_GET_PANEL' "${cfg}" && ok "panel gate enabled" || bad "CTS_MTK_GET_PANEL not defined"
grep -q '^#define CFG_CTS_PANEL_MATCH.*"icnl9911"' "${cfg}" && ok "panel match is icnl9911" || bad "CFG_CTS_PANEL_MATCH wrong"
grep -q 'ret = cts_get_panel();' "${drv}" && ok "gate called from probe" || bad "gate not called from probe"
if grep -q 'saved_command_line' "${drv}"; then bad "legacy cmdline panel parser still present"; else ok "legacy cmdline panel parser removed"; fi

# Ilitek must step aside on the Chipone panel.
grep -q 'mtk_lcm_gate_is_active(ILITEK_FOREIGN_PANEL)' "${TS_DIR}/ILITEK_V2/ilitek_plat_mtk.c" \
    && ok "ilitek gate present" || bad "ilitek gate missing"

echo "== 3. kernel object build (optional)"
# mtkfb.o cannot be built standalone (its include paths come from the full
# build), so the exported symbol is checked at its definition instead.
if grep -q '^char mtkfb_lcm_name\[' "${TS_DIR}/../../misc/mediatek/video/mt6765/videox/mtkfb.c"; then
    ok "mtkfb_lcm_name is a non-static global in videox/mtkfb.c"
else
    bad "mtkfb_lcm_name definition not found / became static"
fi
if [[ -n "${KERNEL_O:-}" ]]; then
    ksrc="$(cd "${TS_DIR}/../../.." && pwd)"
    cc="${CROSS_COMPILE:-aarch64-linux-gnu-}"
    for obj in drivers/input/touchscreen/chipone_ICNL_9916/cts_i2c_driver.o \
               drivers/input/touchscreen/ILITEK_V2/ilitek_plat_mtk.o; do
        # Force a recompile so a stale object cannot mask a regression.
        touch "${ksrc}/${obj%.o}.c"
        if (cd "${ksrc}" && make -s ARCH=arm64 CROSS_COMPILE="${cc}" HOSTCC=gcc \
                O="${KERNEL_O}" "${obj}" >"${WORK}/mk.log" 2>&1); then
            if grep -E 'warning:|error:' "${WORK}/mk.log" | grep -v 'scripts/' | grep -q .; then
                grep -E 'warning:|error:' "${WORK}/mk.log" | grep -v 'scripts/' >&2
                bad "${obj##*/} builds with warnings"
            else
                ok "${obj##*/} builds clean"
            fi
            if "${cc}nm" "${KERNEL_O}/${obj}" | grep -q ' U mtkfb_lcm_name'; then
                ok "${obj##*/} links the LCM name gate"
            else
                bad "${obj##*/} does not reference mtkfb_lcm_name"
            fi
        else
            tail -5 "${WORK}/mk.log" >&2; bad "${obj##*/} failed to build"
        fi
    done
else
    echo "  skipped (set KERNEL_O to enable the object builds)"
fi

echo
[[ ${FAIL} -eq 0 ]] && echo "ALL PASSED" || echo "${FAIL} FAILURE(S)"
[[ ${FAIL} -eq 0 ]]

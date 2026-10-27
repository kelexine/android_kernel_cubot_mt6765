// SPDX-License-Identifier: GPL-2.0
/*
 * Host unit tests for mtk_lcm_gate.h.
 *
 * Author: kelexine <https://github.com/kelexine>
 *
 * Build/run: tests/run_tests.sh   (no kernel tree needed)
 */
#define MTK_LCM_GATE_HOST_TEST 1
#include <stdio.h>
#include "../mtk_lcm_gate.h"

#define CHIPONE_MATCH "icnl9911"

/* Every LCM driver listed in CONFIG_CUSTOM_KERNEL_LCM of marlon_defconfig. */
static const char *const LCMS[] = {
	"ili9881h_hjc6217_haifei_hdplus1520",
	"icnl9911_boe621_haifei_lhd",
	"nt35695_fhd_dsi_vdo_truly_rt5081_hdp",
	"nt35695B_fhd_dsi_vdo_auo_rt5081_hdp",
};

static int failures;
static int checks;

#define CHECK(cond, ...)                                                   \
	do {                                                               \
		checks++;                                                  \
		if (!(cond)) {                                             \
			failures++;                                        \
			printf("  FAIL %s:%d: ", __FILE__, __LINE__);      \
			printf(__VA_ARGS__);                               \
			printf("\n");                                      \
		}                                                          \
	} while (0)

static void test_eval_require_basic(void)
{
	printf("eval_require basics\n");
	CHECK(mtk_lcm_gate_eval_require("icnl9911_boe621_haifei_lhd",
					CHIPONE_MATCH) == 0, "match binds");
	CHECK(mtk_lcm_gate_eval_require("ili9881h_hjc6217_haifei_hdplus1520",
					CHIPONE_MATCH) == -ENODEV,
	      "other panel is -ENODEV");
	CHECK(mtk_lcm_gate_eval_require("", CHIPONE_MATCH) == -EPROBE_DEFER,
	      "empty name defers");
	CHECK(mtk_lcm_gate_eval_require(NULL, CHIPONE_MATCH) == -EPROBE_DEFER,
	      "NULL name defers");
	CHECK(mtk_lcm_gate_eval_require("anything", NULL) == 0,
	      "NULL needle disables the gate");
	CHECK(mtk_lcm_gate_eval_require("anything", "") == 0,
	      "empty needle disables the gate");
	CHECK(mtk_lcm_gate_eval_require(NULL, NULL) == 0,
	      "no needle wins over unknown name");
}

static void test_name_matches(void)
{
	printf("name_matches\n");
	CHECK(mtk_lcm_gate_name_matches("icnl9911_x", "icnl9911"), "substring");
	CHECK(mtk_lcm_gate_name_matches("x_icnl9911", "icnl9911"),
	      "match is not anchored to the start");
	CHECK(!mtk_lcm_gate_name_matches("ili9881h", "icnl9911"), "mismatch");
	CHECK(!mtk_lcm_gate_name_matches(NULL, "icnl9911"), "NULL active");
	CHECK(!mtk_lcm_gate_name_matches("", "icnl9911"), "empty active");
	CHECK(!mtk_lcm_gate_name_matches("icnl9911", NULL), "NULL needle");
	CHECK(!mtk_lcm_gate_name_matches("icnl9911", ""), "empty needle");
}

/*
 * Full policy table. Chipone driver: bind only on icnl9911.
 * Ilitek driver: refuse only when icnl9911 is known to be fitted.
 */
static void test_policy_over_all_lcms(void)
{
	size_t i;

	printf("policy over every LCM in CUSTOM_KERNEL_LCM\n");
	for (i = 0; i < sizeof(LCMS) / sizeof(LCMS[0]); i++) {
		int is_chipone = strstr(LCMS[i], "icnl9911") != NULL;
		int chipone = mtk_lcm_gate_eval_require(LCMS[i], CHIPONE_MATCH);
		int ilitek_refuses =
			mtk_lcm_gate_name_matches(LCMS[i], "icnl9911");

		CHECK((chipone == 0) == is_chipone,
		      "chipone binding wrong for %s (ret=%d)", LCMS[i], chipone);
		CHECK(ilitek_refuses == is_chipone,
		      "ilitek refusal wrong for %s", LCMS[i]);
	}
}

static void test_ilitek_fails_open(void)
{
	printf("ilitek keeps old behaviour while the name is unknown\n");
	CHECK(!mtk_lcm_gate_name_matches(NULL, "icnl9911"), "NULL");
	CHECK(!mtk_lcm_gate_name_matches("", "icnl9911"), "empty");
}

static void test_no_name_source_is_open(void)
{
	printf("configs without mtkfb leave the gates open\n");
	/* MTK_LCM_GATE_HAVE_NAME is undefined under the host-test define. */
	CHECK(mtk_lcm_gate_require(CHIPONE_MATCH) == 0, "require is open");
	CHECK(!mtk_lcm_gate_is_active("icnl9911"), "is_active is false");
}

int main(void)
{
	test_eval_require_basic();
	test_name_matches();
	test_policy_over_all_lcms();
	test_ilitek_fails_open();
	test_no_name_source_is_open();

	printf("\n%d checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}

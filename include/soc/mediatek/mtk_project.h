#ifndef _MTK_PROJECT_H_
#define _MTK_PROJECT_H_
#include "mtk_project_data_ocdt.h"
#include "mtk_project_oldcdt.h"

#define ALIGN4(s) ((sizeof(s) + 3)&(~0x3))

#define FEATURE1_OPEARTOR_OPEN_MASK 0000
#define FEATURE1_FOREIGN_MASK 0001
#define FEATURE1_OPEARTOR_CMCC_MASK 0010
#define FEATURE1_OPEARTOR_CT_MASK 0011
#define FEATURE1_OPEARTOR_CU_MASK 0100
#define FEATURE1_OPEARTOR_MAX_MASK 1111


enum F_INDEX {
	IDX_1 = 1,
	IDX_2,
	IDX_3,
	IDX_4,
	IDX_5,
	IDX_6,
	IDX_7,
	IDX_8,
	IDX_9,
	IDX_10,
};

struct pcb_match {
	enum PCB_VERSION version;
	char *str;
};
static inline unsigned int get_cdt_version(void) { return 0; }
static inline unsigned int get_eng_version(void) { return 0; }
static inline unsigned int is_new_cdt(void) { return 0; }

//cdt interface for Q or R
static inline unsigned int get_project(void) { return 0; }
static inline unsigned int is_project(int project) { return 0; }
static inline unsigned int get_Oppo_Boot_Mode(void) { return 0; }
static inline unsigned int get_PCB_Version(void) { return 0; }
static inline unsigned int get_audio(void) { return 0; }
static inline unsigned int get_dtsiNo(void) { return 0; }
static inline uint32_t get_oppo_feature(enum F_INDEX index) { return 0; }

//cdt interface for P->R
static inline int32_t get_Modem_Version(void) { return 0; }
static inline int32_t get_Operator_Version(void) { return 0; }

//eng cdt data for P or Q or R
static inline bool is_confidential(void) { return false; }
static inline bool oppo_daily_build(void) { return false; }

#endif

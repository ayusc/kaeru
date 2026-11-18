#ifndef _LIB_DUALBOOT_H_
#define _LIB_DUALBOOT_H_

#include <lib/common.h>

#define DUALBOOT_ENV_TARGET "kaeru_dualboot"
#define DUALBOOT_ENV_SPLIT  "kaeru_userdata_split"

#define KAERU_ENV_BLDR_SPOOF_1 "kaeru_bldr_spoof_1"
#define KAERU_ENV_BLDR_SPOOF_2 "kaeru_bldr_spoof_2"

enum dualboot_target {
    DUALBOOT_SLOT1 = 1,
    DUALBOOT_SLOT2 = 2,
};

enum dualboot_target dualboot_get_target(void);
int dualboot_set_target(enum dualboot_target target);
void dualboot_apply_cmdline(char* cmdline);
void dualboot_init(void);

int dualboot_split_userdata(uint32_t slot1_gb, uint64_t *out_s1_sec, uint64_t *out_s2_sec, uint32_t *out_s1_gb, uint32_t *out_s2_gb);
int dualboot_restore_userdata(void);

#endif

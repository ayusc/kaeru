
//
// SPDX-FileCopyrightText: 2026 R0rt1z2 <roger@r0rt1z2.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <lib/spoof.h>
#include <lib/common.h>
#include <lib/dualboot.h>

int is_spoofing_enabled_slot(int slot) {
    const char *key = (slot == 2) ? KAERU_ENV_BLDR_SPOOF_2 : KAERU_ENV_BLDR_SPOOF_1;
    const char *val = get_env((char*)key);
    if (val && val[0])
        return strcmp(val, "1") == 0;
    const char *legacy = get_env(KAERU_ENV_BLDR_SPOOF);
    return legacy && strcmp(legacy, "1") == 0;
}

int is_spoofing_enabled(void) {
    enum dualboot_target target = dualboot_get_target();
    return is_spoofing_enabled_slot(target == DUALBOOT_SLOT2 ? 2 : 1);
}

int get_lock_state(uint32_t *lock_state) {
    int spoofing = is_spoofing_enabled();
#if KAERU_DEBUG
    printf("Attempted to get lock state, spoofing is %s\n",
            spoofing ? "enabled" : "disabled");
#endif
    *lock_state = spoofing ? LKS_LOCK : LKS_UNLOCK;
    return 0;
}

int sec_usbdl_enabled(void) {
    return 0;
}

unsigned int seclib_sec_boot_enabled(unsigned int) {
    return 0;
}

unsigned get_unlocked_status(void) {
    return 1;
}

void cmd_spoof_bootloader_lock(const char *arg, void *data, unsigned sz) {
    char action[16];
    char slot_tok[16];
    char line[FASTBOOT_INFO_MAX + 1];

    (void)data;
    (void)sz;

    arg = next_token(arg, action, sizeof(action));
    next_token(arg, slot_tok, sizeof(slot_tok));

    if (!strcmp(action, "status")) {
        int s1 = is_spoofing_enabled_slot(1);
        int s2 = is_spoofing_enabled_slot(2);
        npf_snprintf(line, sizeof(line), "Slot 1 (Slot A): lock spoof %s", s1 ? "enabled" : "disabled");
        fastboot_info(line);
        npf_snprintf(line, sizeof(line), "Slot 2 (Slot B): lock spoof %s", s2 ? "enabled" : "disabled");
        fastboot_info(line);
        fastboot_okay("");
        return;
    }

    int target = -1;
    if (!strcmp(action, "on"))
        target = 1;
    else if (!strcmp(action, "off"))
        target = 0;

    if (target != -1) {
        if (!strcmp(slot_tok, "1")) {
            set_env(KAERU_ENV_BLDR_SPOOF_1, target ? "1" : "0");
            npf_snprintf(line, sizeof(line), "Slot 1 lock spoof %s.", target ? "enabled" : "disabled");
            fastboot_info(line);
            fastboot_okay("");
            return;
        }
        if (!strcmp(slot_tok, "2")) {
            set_env(KAERU_ENV_BLDR_SPOOF_2, target ? "1" : "0");
            npf_snprintf(line, sizeof(line), "Slot 2 lock spoof %s.", target ? "enabled" : "disabled");
            fastboot_info(line);
            fastboot_okay("");
            return;
        }

        fastboot_info("kaeru bootloader lock spoofing control");
        fastboot_info("");
        fastboot_info("When enabled, device reports as 'locked' to TEE");
        fastboot_info("while maintaining full fastboot and root capabilities.");
        fastboot_info("");
        fastboot_info("Commands:");
        fastboot_info("  on <1|2>  - Enable spoofing for slot (Data format required)");
        fastboot_info("  off <1|2> - Disable spoofing for slot (Data format required)");
        fastboot_info("  status    - Show current state for both slots");
        fastboot_fail("Usage: fastboot oem bldr_spoof <on|off> <1|2> or status");
        return;
    }

    fastboot_info("kaeru bootloader lock spoofing control");
    fastboot_info("");
    fastboot_info("When enabled, device reports as 'locked' to TEE");
    fastboot_info("while maintaining full fastboot and root capabilities.");
    fastboot_info("");
    fastboot_info("Commands:");
    fastboot_info("  on <1|2>  - Enable spoofing for slot (Data format required)");
    fastboot_info("  off <1|2> - Disable spoofing for slot (Data format required)");
    fastboot_info("  status    - Show current state for both slots");
    fastboot_fail("Usage: fastboot oem bldr_spoof <on|off> <1|2> or status");
}
#include <lib/bootargs.h>
#include <lib/debug.h>
#include <lib/dualboot.h>
#include <lib/environment.h>
#include <lib/fastboot.h>
#include <lib/spoof.h>
#include <lib/string.h>

static void format_size(uint64_t bytes, char* out, size_t out_len) {
    uint32_t mb = (uint32_t)(bytes / (1024 * 1024));
    uint32_t gb_whole = mb / 1024;
    uint32_t gb_frac = ((mb % 1024) * 100) / 1024;

    npf_snprintf(out, out_len, "%u.%02u GiB (%u MiB)", (unsigned)gb_whole,
                 (unsigned)gb_frac, (unsigned)mb);
}

static uint64_t get_part_size(const char* name) {
#ifdef CONFIG_PARTITION_GET_SIZE_BY_NAME_ADDRESS
    return ((uint64_t (*)(const char*))(CONFIG_PARTITION_GET_SIZE_BY_NAME_ADDRESS | 1))(name);
#else
    (void)name;
    return 0;
#endif
}

static void cmd_set_boot(const char* arg, void* data, unsigned sz) {
    char tok[16];

    (void)data;
    (void)sz;

    next_token(arg, tok, sizeof(tok));

    if (!strcmp(tok, "1")) {
        if (dualboot_set_target(DUALBOOT_SLOT1) < 0) {
            fastboot_fail("Failed to set boot target");
            return;
        }
        fastboot_info("Boot target set to: Slot 1 (Slot A)");
        fastboot_info("Active slot set to: a");
        fastboot_info("Active data partition: userdata (Slot 1)");
        fastboot_okay("");
        return;
    }

    if (!strcmp(tok, "2")) {
        if (dualboot_set_target(DUALBOOT_SLOT2) < 0) {
            fastboot_fail("Failed to set boot target");
            return;
        }
        fastboot_info("Boot target set to: Slot 2 (Slot B)");
        fastboot_info("Active slot set to: b");
        fastboot_info("Active data partition: userdata (Slot 2)");
        fastboot_okay("");
        return;
    }

    fastboot_info("kaeru dual boot target selection");
    fastboot_info("");
    fastboot_info("Sets default persistent boot slot and syncs active slot.");
    fastboot_info("");
    fastboot_info("Slots:");
    fastboot_info("  1 - Slot A (uses primary userdata)");
    fastboot_info("  2 - Slot B (uses secondary userdata2)");
    fastboot_fail("Usage: fastboot oem set-boot <1|2>");
}

static void cmd_show_active(const char* arg, void* data, unsigned sz) {
    char line[FASTBOOT_INFO_MAX + 1];

    (void)arg;
    (void)data;
    (void)sz;

    enum dualboot_target target = dualboot_get_target();
    int is_slot2 = (target == DUALBOOT_SLOT2);
    int spoof = is_spoofing_enabled_slot(is_slot2 ? 2 : 1);

    npf_snprintf(line, sizeof(line), "Dual boot target : Slot %d (%s)",
                 is_slot2 ? 2 : 1, is_slot2 ? "Slot B" : "Slot A");
    fastboot_info(line);

    npf_snprintf(line, sizeof(line), "Active slot      : %c", is_slot2 ? 'b' : 'a');
    fastboot_info(line);

    npf_snprintf(line, sizeof(line), "Data partition   : userdata (Slot %d)", is_slot2 ? 2 : 1);
    fastboot_info(line);

    npf_snprintf(line, sizeof(line), "Lock spoof       : %s", spoof ? "enabled" : "disabled");
    fastboot_info(line);

    fastboot_okay("");
}

static void show_single_part_size(const char* name) {
    char sz_str[32];
    char line[FASTBOOT_INFO_MAX + 1];
    uint64_t bytes = get_part_size(name);

    if (bytes == 0 || bytes == (uint64_t)-1) {
        npf_snprintf(line, sizeof(line), "%-16s: not found", name);
        fastboot_info(line);
        return;
    }

    format_size(bytes, sz_str, sizeof(sz_str));
    npf_snprintf(line, sizeof(line), "%-16s: %s", name, sz_str);
    fastboot_info(line);
}

static void cmd_check_size(const char* arg, void* data, unsigned sz) {
    char part[40];

    (void)data;
    (void)sz;

    next_token(arg, part, sizeof(part));
    if (!part[0] || !strcmp(part, "all")) {
        fastboot_info("Partition sizes:");
        show_single_part_size("userdata");
        show_single_part_size("userdata1");
        show_single_part_size("userdata2");
        show_single_part_size("cust");
        show_single_part_size("super");
        show_single_part_size("boot_a");
        show_single_part_size("boot_b");
        fastboot_okay("");
        return;
    }

    uint64_t bytes = get_part_size(part);
    if (bytes == 0 || bytes == (uint64_t)-1) {
        fastboot_info("kaeru partition size inspector");
        fastboot_info("");
        fastboot_info("Partition not found on storage.");
        fastboot_info("");
        fastboot_info("Commands:");
        fastboot_info("  check-size <partition> - Check specific partition");
        fastboot_info("  check-size all         - Check common dual boot partitions");
        fastboot_fail("Usage: fastboot oem check-size [partition|all]");
        return;
    }

    char sz_str[32];
    char line[FASTBOOT_INFO_MAX + 1];
    format_size(bytes, sz_str, sizeof(sz_str));
    npf_snprintf(line, sizeof(line), "%s size: %s", part, sz_str);
    fastboot_info(line);
    fastboot_okay("");
}

static void cmd_resize(const char* arg, void* data, unsigned sz) {
    char slot_tok[16];
    char size_tok[16];
    char line[FASTBOOT_INFO_MAX + 1];

    (void)data;
    (void)sz;

    arg = next_token(arg, slot_tok, sizeof(slot_tok));
    next_token(arg, size_tok, sizeof(size_tok));

    if (!strcmp(slot_tok, "restore")) {
        int ret = dualboot_restore_userdata();
        if (ret < 0) {
            npf_snprintf(line, sizeof(line), "Failed to restore userdata (err %d)", ret);
            fastboot_fail(line);
            return;
        }
        fastboot_info("Userdata restored to single partition layout");
        fastboot_info("Format / wipe of data required in recovery");
        fastboot_okay("");
        return;
    }

    uint64_t total_bytes = get_part_size("userdata");
    uint64_t u2_bytes = get_part_size("userdata2");
    if (u2_bytes && u2_bytes != (uint64_t)-1)
        total_bytes += u2_bytes;
    uint64_t u1_bytes = get_part_size("userdata1");
    if (u1_bytes && u1_bytes != (uint64_t)-1)
        total_bytes += u1_bytes;

    if (total_bytes == 0 || total_bytes == (uint64_t)-1) {
        fastboot_fail("userdata partition not found");
        return;
    }

    uint64_t total_gb = total_bytes / (1024ULL * 1024ULL * 1024ULL);

    if (!slot_tok[0]) {
        npf_snprintf(line, sizeof(line), "Total userdata: %u GiB", (unsigned)total_gb);
        fastboot_info(line);
        char* cur = get_env(DUALBOOT_ENV_SPLIT);
        if (cur && cur[0]) {
            npf_snprintf(line, sizeof(line), "Saved split: %s GiB for Slot 1", cur);
            fastboot_info(line);
        }
        fastboot_info("");
        fastboot_info("Commands:");
        fastboot_info("  resize 1 <size_gb> - Set Slot 1 size");
        fastboot_info("  resize 2 <size_gb> - Set Slot 2 size");
        fastboot_info("  The other slot gets the remaining !");
        fastboot_info("  resize restore    - Restore to single userdata");
        fastboot_fail("Usage: fastboot oem resize <1|2|restore> [size_in_gb]");
        return;
    }

    int slot = 0;
    if (!strcmp(slot_tok, "1"))
        slot = 1;
    else if (!strcmp(slot_tok, "2"))
        slot = 2;
    else {
        fastboot_info("kaeru userdata partition resize control");
        fastboot_info("");
        fastboot_info("Slot must be 1 (Slot A), 2 (Slot B), or restore.");
        fastboot_fail("Usage: fastboot oem resize <1|2|restore> [size_in_gb]");
        return;
    }

    if (!size_tok[0]) {
        fastboot_info("kaeru userdata partition resize control");
        fastboot_info("");
        fastboot_info("Please specify target size in GiB.");
        fastboot_fail("Usage: fastboot oem resize <1|2> <size_in_gb>");
        return;
    }

    uint32_t val_gb = 0;
    const char* p = size_tok;
    while (*p >= '0' && *p <= '9') {
        val_gb = val_gb * 10 + (*p - '0');
        p++;
    }

    if (val_gb == 0 || (uint64_t)val_gb >= total_gb) {
        npf_snprintf(line, sizeof(line), "Size must be between 1 and %u GiB", (unsigned)(total_gb - 1));
        fastboot_fail(line);
        return;
    }

    uint32_t slot1_gb;
    if (slot == 1) {
        slot1_gb = val_gb;
    } else {
        slot1_gb = (uint32_t)(total_gb - val_gb);
    }

    uint64_t s1_sec = 0;
    uint64_t s2_sec = 0;
    uint32_t s1_gb = 0;
    uint32_t s2_gb = 0;

    int ret = dualboot_split_userdata(slot1_gb, &s1_sec, &s2_sec, &s1_gb, &s2_gb);
    if (ret < 0) {
        npf_snprintf(line, sizeof(line), "Failed to repartition GPT (err %d)", ret);
        fastboot_fail(line);
        return;
    }

    npf_snprintf(line, sizeof(line), "Total userdata: %u GiB (%lu sectors)",
                 (unsigned)total_gb, (unsigned long)(s1_sec + s2_sec));
    fastboot_info(line);

    npf_snprintf(line, sizeof(line), "Slot 1 (userdata):  %u GiB (%lu sectors)",
                 (unsigned)s1_gb, (unsigned long)s1_sec);
    fastboot_info(line);

    npf_snprintf(line, sizeof(line), "Slot 2 (userdata2): %u GiB (%lu sectors)",
                 (unsigned)s2_gb, (unsigned long)s2_sec);
    fastboot_info(line);

    fastboot_info("GPT partition table updated on flash!");
    fastboot_info("Format / wipe of data is required in recovery.");

    fastboot_okay("");
}

FASTBOOT_CMD(set_boot, "oem set-boot", cmd_set_boot, 1);
FASTBOOT_CMD(show_active, "oem show-active", cmd_show_active, 1);
FASTBOOT_CMD(check_size, "oem check-size", cmd_check_size, 1);
FASTBOOT_CMD(resize, "oem resize", cmd_resize, 1);

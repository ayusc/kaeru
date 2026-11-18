#include <lib/bootargs.h>
#include <lib/debug.h>
#include <lib/dualboot.h>
#include <lib/environment.h>
#include <lib/fastboot.h>
#include <lib/mt_part.h>
#include <lib/storage/gpt.h>
#include <lib/string.h>

#define GPT_TOTAL_BYTES 17408

static uint8_t gpt_buffer[GPT_TOTAL_BYTES] __attribute__((aligned(64)));

static int gpt_match_name(const struct gpt_entry *e, const char *name) {
    for (int i = 0; i < GPT_NAME_MAX; i++) {
        if (!name[i])
            return (e->name[i] == 0);
        if (e->name[i] != (uint16_t)(uint8_t)name[i])
            return 0;
    }
    return 1;
}

static void gpt_set_name(struct gpt_entry *e, const char *name) {
    memset(e->name, 0, sizeof(e->name));
    for (int i = 0; i < (GPT_NAME_MAX - 1) && name[i]; i++) {
        e->name[i] = (uint16_t)(uint8_t)name[i];
    }
}

static int guid_is_zero(const uint8_t *g) {
    for (int i = 0; i < 16; i++) {
        if (g[i] != 0)
            return 0;
    }
    return 1;
}

static int dualboot_read_gpt(uint8_t *buf, size_t size) {
    struct device_t *dev = mt_part_get_device();
    if (!dev)
        return -1;

    uint8_t *info = (uint8_t *)dev->blkdev;
    uint32_t part_id = info ? info[0x1f] : 0;

#ifdef CONFIG_READ_BLOCK_ADDRESS
    int ret = ((int (*)(void*, uint32_t, uint64_t, uint32_t))(CONFIG_READ_BLOCK_ADDRESS | 1))(buf, part_id, 0ULL, size);
    if (ret != 0) {
        if (((int (*)(void*, uint32_t, uint64_t, uint32_t))(CONFIG_READ_BLOCK_ADDRESS | 1))(buf, part_id, 0ULL, 512) != 0)
            return -2;
        if (((int (*)(void*, uint32_t, uint64_t, uint32_t))(CONFIG_READ_BLOCK_ADDRESS | 1))(buf + 512, part_id, 1ULL, 512) != 0)
            return -3;
        if (((int (*)(void*, uint32_t, uint64_t, uint32_t))(CONFIG_READ_BLOCK_ADDRESS | 1))(buf + 1024, part_id, 2ULL, size - 1024) != 0)
            return -4;
    }
#else
    if (!dev->read)
        return -2;
    size_t rd = dev->read(dev, 0ULL, buf, size, part_id);
    if (rd != size)
        return -3;
#endif

    struct gpt_header *hdr = (struct gpt_header *)(buf + GPT_BLOCK_SIZE);
    if (memcmp(hdr->signature, "EFI PART", 8) != 0)
        return -5;

    return 0;
}

static int dualboot_write_gpt(uint8_t *buf, size_t size) {
#ifdef CONFIG_GPT_UPDATE_ADDRESS
    return ((int (*)(void*, unsigned))(CONFIG_GPT_UPDATE_ADDRESS | 1))(buf, size);
#else
    (void)buf;
    (void)size;
    return -1;
#endif
}

static int dualboot_find_data_entries(struct gpt_entry *entries, uint32_t count,
                                      struct gpt_entry **p1, struct gpt_entry **p2,
                                      struct gpt_entry **free_e) {
    struct gpt_entry *found[2] = {0, 0};
    int found_cnt = 0;

    if (free_e)
        *free_e = NULL;

    for (uint32_t i = 0; i < count; i++) {
        struct gpt_entry *e = &entries[i];

        if (free_e && !*free_e && guid_is_zero(e->type_guid))
            *free_e = e;

        if (gpt_match_name(e, "userdata") ||
            gpt_match_name(e, "userdata1") ||
            gpt_match_name(e, "userdata2")) {
            if (found_cnt < 2)
                found[found_cnt++] = e;
        }
    }

    if (found_cnt == 0)
        return -1;

    if (found_cnt == 1) {
        *p1 = found[0];
        *p2 = NULL;
        return 1;
    }

    if (found[0]->first_lba <= found[1]->first_lba) {
        *p1 = found[0];
        *p2 = found[1];
    } else {
        *p1 = found[1];
        *p2 = found[0];
    }

    return 2;
}

static int dualboot_sync_partitions(enum dualboot_target target) {
    if (dualboot_read_gpt(gpt_buffer, GPT_TOTAL_BYTES) < 0)
        return -1;

    struct gpt_header *hdr = (struct gpt_header *)(gpt_buffer + GPT_BLOCK_SIZE);
    struct gpt_entry *entries = (struct gpt_entry *)(gpt_buffer + (GPT_BLOCK_SIZE * 2));
    struct gpt_entry *p1 = NULL;
    struct gpt_entry *p2 = NULL;

    int cnt = dualboot_find_data_entries(entries, hdr->entry_count, &p1, &p2, NULL);
    if (cnt != 2 || !p1 || !p2)
        return 0;

    int need_write = 0;
    if (target == DUALBOOT_SLOT2) {
        if (!gpt_match_name(p1, "userdata1") || !gpt_match_name(p2, "userdata")) {
            gpt_set_name(p1, "userdata1");
            gpt_set_name(p2, "userdata");
            need_write = 1;
        }
    } else {
        if (!gpt_match_name(p1, "userdata") || !gpt_match_name(p2, "userdata2")) {
            gpt_set_name(p1, "userdata");
            gpt_set_name(p2, "userdata2");
            need_write = 1;
        }
    }

    if (need_write)
        return dualboot_write_gpt(gpt_buffer, GPT_TOTAL_BYTES);

    return 0;
}

int dualboot_split_userdata(uint32_t slot1_gb, uint64_t *out_s1_sec, uint64_t *out_s2_sec,
                            uint32_t *out_s1_gb, uint32_t *out_s2_gb) {
    int ret = dualboot_read_gpt(gpt_buffer, GPT_TOTAL_BYTES);
    if (ret < 0)
        return ret;

    struct gpt_header *hdr = (struct gpt_header *)(gpt_buffer + GPT_BLOCK_SIZE);
    struct gpt_entry *entries = (struct gpt_entry *)(gpt_buffer + (GPT_BLOCK_SIZE * 2));
    struct gpt_entry *p1 = NULL;
    struct gpt_entry *p2 = NULL;
    struct gpt_entry *free_e = NULL;

    int cnt = dualboot_find_data_entries(entries, hdr->entry_count, &p1, &p2, &free_e);
    if (cnt <= 0 || !p1)
        return -10;

    if (!p2 && !free_e)
        return -11;

    uint64_t total_start = p1->first_lba;
    uint64_t total_end = p2 ? p2->last_lba : p1->last_lba;
    uint64_t total_sectors = total_end - total_start + 1;
    uint64_t total_gb = (total_sectors * 512ULL) / (1024ULL * 1024ULL * 1024ULL);

    if (slot1_gb == 0 || (uint64_t)slot1_gb >= total_gb)
        return -12;

    if (!p2) {
        p2 = free_e;
        memcpy(p2->type_guid, p1->type_guid, 16);
        memcpy(p2->part_guid, p1->part_guid, 16);
        p2->part_guid[15] ^= 0x5a;
        p2->attributes = p1->attributes;
    }

    uint64_t s1_sec = ((uint64_t)slot1_gb * 1024ULL * 1024ULL * 1024ULL) / 512ULL;
    s1_sec = (s1_sec + 7) & ~7ULL;
    uint64_t s2_sec = total_sectors - s1_sec;

    p1->first_lba = total_start;
    p1->last_lba = total_start + s1_sec - 1;

    p2->first_lba = p1->last_lba + 1;
    p2->last_lba = total_end;

    enum dualboot_target target = dualboot_get_target();
    if (target == DUALBOOT_SLOT2) {
        gpt_set_name(p1, "userdata1");
        gpt_set_name(p2, "userdata");
    } else {
        gpt_set_name(p1, "userdata");
        gpt_set_name(p2, "userdata2");
    }

    ret = dualboot_write_gpt(gpt_buffer, GPT_TOTAL_BYTES);
    if (ret < 0)
        return -13;

    char split_val[16];
    npf_snprintf(split_val, sizeof(split_val), "%u", (unsigned)slot1_gb);
    set_env(DUALBOOT_ENV_SPLIT, split_val);

    if (out_s1_sec)
        *out_s1_sec = s1_sec;
    if (out_s2_sec)
        *out_s2_sec = s2_sec;
    if (out_s1_gb)
        *out_s1_gb = slot1_gb;
    if (out_s2_gb)
        *out_s2_gb = (uint32_t)(total_gb - slot1_gb);

    return 0;
}

int dualboot_restore_userdata(void) {
    int ret = dualboot_read_gpt(gpt_buffer, GPT_TOTAL_BYTES);
    if (ret < 0)
        return ret;

    struct gpt_header *hdr = (struct gpt_header *)(gpt_buffer + GPT_BLOCK_SIZE);
    struct gpt_entry *entries = (struct gpt_entry *)(gpt_buffer + (GPT_BLOCK_SIZE * 2));
    struct gpt_entry *p1 = NULL;
    struct gpt_entry *p2 = NULL;

    int cnt = dualboot_find_data_entries(entries, hdr->entry_count, &p1, &p2, NULL);
    if (cnt <= 0 || !p1)
        return -10;

    if (p2) {
        p1->last_lba = p2->last_lba;
        gpt_set_name(p1, "userdata");
        memset(p2, 0, sizeof(*p2));
        ret = dualboot_write_gpt(gpt_buffer, GPT_TOTAL_BYTES);
        if (ret < 0)
            return -13;
    } else {
        gpt_set_name(p1, "userdata");
    }

    set_env(DUALBOOT_ENV_SPLIT, "");
    return 0;
}

enum dualboot_target dualboot_get_target(void) {
    char* target = get_env(DUALBOOT_ENV_TARGET);
    if (target && !strcmp(target, "2"))
        return DUALBOOT_SLOT2;

    return DUALBOOT_SLOT1;
}

int dualboot_set_target(enum dualboot_target target) {
    const char* val = (target == DUALBOOT_SLOT2) ? "2" : "1";
    int ret = set_env(DUALBOOT_ENV_TARGET, (char*)val);
    if (!ret) {
        fastboot_publish("kaeru-boot-target", val);
        fastboot_publish("current-slot", (target == DUALBOOT_SLOT2) ? "b" : "a");
        dualboot_sync_partitions(target);
    }
    return ret;
}

void dualboot_apply_cmdline(char* cmdline) {
    if (!cmdline)
        return;

    enum dualboot_target target = dualboot_get_target();

    if (target == DUALBOOT_SLOT2) {
        cmdline_replace(cmdline, "androidboot.slot_suffix=", "_a", "_b");
    } else {
        cmdline_replace(cmdline, "androidboot.slot_suffix=", "_b", "_a");
    }
}

void dualboot_init(void) {
    enum dualboot_target target = dualboot_get_target();
    const char* str = (target == DUALBOOT_SLOT2) ? "2" : "1";
    fastboot_publish("kaeru-boot-target", str);
    fastboot_publish("current-slot", (target == DUALBOOT_SLOT2) ? "b" : "a");
}

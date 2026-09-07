/*
 * Pack Store (H6.1 M2) — named Lua tool packs in LittleFS + boot autorun.
 * See interfaces/pack_if.h for the contract. One buffer serves upload,
 * run and boot (the CLI task is the single user; exec takes the engine
 * lock internally).
 */
#include "pack_if.h"
#include "storage_if.h"
#include "lua_if.h"
#include "bridge_if.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define PACKS_DIR   "/littlefs/packs"
#define PACK_EXT    ".lua"
#define PACK_AR_EXT ".autorun"

/*
 * Dirent budget for one full listing: every autorun pack stores TWO files
 * (<name>.lua + <name>.autorun), so reading only PACK_MAX_FILES dirents
 * silently drops packs once markers exist (audit B3). The upload path
 * enforces the pack cap, so 2x is always enough for the whole store.
 */
#define PACK_LIST_DIRENTS (PACK_MAX_FILES * 2)

/* Upload/run scratch. -1 reserves the NUL terminator for exec paths. */
static uint8_t  s_buf[PACK_MAX_SIZE];
static uint32_t s_len = 0;
static bool     s_uploading = false;
static bool     s_autorun_pending = false;
static char     s_name[PACK_MAX_NAME + 1] = {0};

bool pack_store_is_uploading(void)
{
    return s_uploading;
}

void pack_store_abort(void)
{
    s_uploading = false;
    s_len = 0;
}

bool pack_store_name_ok(const char *name)
{
    if (name == NULL)
        return false;
    size_t n = strlen(name);
    if (n == 0 || n > PACK_MAX_NAME)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

static void s_path(char *out, size_t out_len, const char *name, bool marker)
{
    snprintf(out, out_len, "%s/%s%s", PACKS_DIR, name,
             marker ? PACK_AR_EXT : PACK_EXT);
}

static void s_set_err(char *err, uint16_t err_len, const char *msg)
{
    if (err != NULL && err_len > 0)
        snprintf(err, err_len, "%s", msg);
}

/* Count stored packs (valid <name>.lua dirents); -1 = listing failed. */
static int s_count_packs(void)
{
    storage_dirent_t ents[PACK_LIST_DIRENTS];
    uint8_t count = 0;
    if (storage_list_dir(PACKS_DIR, ents, PACK_LIST_DIRENTS, &count) != 0)
        return -1;
    uint8_t packs = 0;
    for (uint8_t i = 0; i < count; i++) {
        size_t nl = strlen(ents[i].name);
        if (nl <= 4 || strcmp(ents[i].name + nl - 4, PACK_EXT) != 0)
            continue;
        char name[PACK_MAX_NAME + 1];
        if (nl - 4 > PACK_MAX_NAME)
            continue;
        memcpy(name, ents[i].name, nl - 4);
        name[nl - 4] = '\0';
        if (pack_store_name_ok(name))
            packs++;
    }
    return packs;
}

int pack_store_upload_begin(const char *name, bool autorun,
                            char *err, uint16_t err_len)
{
    if (!pack_store_name_ok(name)) {
        s_set_err(err, err_len,
                  "pack name must be [a-zA-Z0-9_], max 24 chars");
        return -620;
    }
    if (s_uploading) {
        s_set_err(err, err_len, "pack upload already in progress");
        return -611;
    }
    /* Enforce the pack cap at the door: a 9th pack could never be seen by
     * LIST or boot autorun (fixed dirent budget), so reject it up front.
     * Overwriting an existing name stays allowed. */
    char path[STORAGE_MAX_PATH_LEN];
    s_path(path, sizeof(path), name, false);
    if (storage_file_exists(path) != 1) {
        int packs = s_count_packs();
        if (packs >= PACK_MAX_FILES) {
            s_set_err(err, err_len,
                      "pack store full (max 8); PACK DEL one first");
            return -624;
        }
    }
    s_uploading = true;
    s_autorun_pending = autorun;
    s_len = 0;
    snprintf(s_name, sizeof(s_name), "%s", name);
    return 0;
}

int pack_store_upload_line(const char *line, uint32_t len,
                           const char **reject_token)
{
    if (!s_uploading)
        return -611;
    if (len == 0)
        return 0;               /* blank lines are legal, store nothing */
    const char *tok = bridge_scan_line(line, len);
    if (tok != NULL) {
        s_uploading = false;    /* fail-closed: abort the session */
        if (reject_token != NULL)
            *reject_token = tok;
        return -612;
    }
    if (s_len + len + 1 > PACK_MAX_SIZE - 1) {
        s_uploading = false;
        return -622;
    }
    memcpy(s_buf + s_len, line, len);
    s_len += len;
    s_buf[s_len++] = '\n';
    return 0;
}

int pack_store_upload_finish(uint32_t *out_size, char *err, uint16_t err_len)
{
    if (!s_uploading) {
        s_set_err(err, err_len, "no pack upload in progress");
        return -611;
    }
    s_uploading = false;
    if (s_len == 0) {
        s_set_err(err, err_len, "empty pack");
        return -612;
    }
    s_buf[s_len] = '\0';
    if (!lua_engine_is_ready()) {
        s_set_err(err, err_len, "Lua engine not initialized");
        return -623;
    }
    char cerr[96];
    if (lua_engine_compile_check((const char *)s_buf, cerr,
                                 sizeof(cerr)) != 0) {
        s_set_err(err, err_len, cerr);
        return -612;
    }
    char path[STORAGE_MAX_PATH_LEN];
    s_path(path, sizeof(path), s_name, false);
    int ret = storage_write_file(path, s_buf, s_len);
    if (ret != 0) {
        snprintf(err, err_len, "storage write failed (%d)", ret);
        return ret;
    }
    s_path(path, sizeof(path), s_name, true);
    if (s_autorun_pending) {
        ret = storage_write_file(path, (const uint8_t *)"1", 1);
        if (ret != 0) {
            snprintf(err, err_len, "autorun marker write failed (%d)", ret);
            return ret;
        }
    } else {
        storage_delete_file(path);   /* clear a stale marker on overwrite */
    }
    if (out_size != NULL)
        *out_size = s_len;
    return 0;
}

int pack_store_run(const char *name, char *result, uint16_t result_len,
                   char *err, uint16_t err_len)
{
    if (s_uploading) {
        s_set_err(err, err_len, "pack upload in progress");
        return -611;
    }
    if (!pack_store_name_ok(name)) {
        s_set_err(err, err_len, "invalid pack name");
        return -620;
    }
    if (!lua_engine_is_ready()) {
        s_set_err(err, err_len, "Lua engine not initialized");
        return -623;
    }
    char path[STORAGE_MAX_PATH_LEN];
    s_path(path, sizeof(path), name, false);
    uint32_t len = 0;
    int ret = storage_read_file(path, s_buf, sizeof(s_buf) - 1, &len);
    if (ret == -707) {
        s_set_err(err, err_len, "pack too large");
        return -622;
    }
    if (ret != 0) {
        s_set_err(err, err_len, "pack not found");
        return -621;
    }
    s_buf[len] = '\0';
    ret = lua_engine_exec((const char *)s_buf, result, result_len);
    if (ret != 0) {
        snprintf(err, err_len, "exec failed (%d)", ret);
        return -613;
    }
    return 0;
}

int pack_store_list(char *json, uint16_t json_len)
{
    if (json == NULL || json_len == 0)
        return -702;
    storage_dirent_t ents[PACK_LIST_DIRENTS];
    uint8_t count = 0;
    int ret = storage_list_dir(PACKS_DIR, ents, PACK_LIST_DIRENTS, &count);
    if (ret == -706)
        count = 0;               /* dir not created yet: empty list */
    else if (ret != 0)
        return ret;
    uint32_t free_b = 0;
    storage_get_free_space(&free_b);
    char path[STORAGE_MAX_PATH_LEN];
    size_t off = (size_t)snprintf(json, json_len,
                                  "{\"status\":\"ok\",\"cmd\":\"pack_list\","
                                  "\"packs\":[");
    uint8_t shown = 0;
    for (uint8_t i = 0; i < count; i++) {
        size_t nl = strlen(ents[i].name);
        if (nl <= 4 || strcmp(ents[i].name + nl - 4, PACK_EXT) != 0)
            continue;            /* markers are not packs */
        char name[PACK_MAX_NAME + 1];
        if (nl - 4 > PACK_MAX_NAME)
            continue;
        memcpy(name, ents[i].name, nl - 4);
        name[nl - 4] = '\0';
        if (!pack_store_name_ok(name))
            continue;
        s_path(path, sizeof(path), name, true);
        bool ar = storage_file_exists(path) == 1;
        int n = snprintf(json + off, json_len - off,
                         "%s{\"name\":\"%s\",\"size\":%lu,\"autorun\":%s}",
                         shown ? "," : "", name,
                         (unsigned long)ents[i].size, ar ? "true" : "false");
        if (n < 0 || (size_t)n >= json_len - off)
            break;               /* out of response budget: stop cleanly */
        off += (size_t)n;
        shown++;
        if (shown >= PACK_MAX_FILES)
            break;
    }
    snprintf(json + off, json_len - off, "],\"free\":%lu}",
             (unsigned long)free_b);
    return 0;
}

int pack_store_del(const char *name)
{
    if (!pack_store_name_ok(name))
        return -620;
    char path[STORAGE_MAX_PATH_LEN];
    s_path(path, sizeof(path), name, false);
    int ret = storage_delete_file(path);
    if (ret != 0)
        return ret;
    s_path(path, sizeof(path), name, true);
    storage_delete_file(path);   /* marker may not exist: fine */
    return 0;
}

int pack_store_set_autorun(const char *name, bool on)
{
    if (!pack_store_name_ok(name))
        return -620;
    char path[STORAGE_MAX_PATH_LEN];
    s_path(path, sizeof(path), name, false);
    if (storage_file_exists(path) != 1)
        return -621;
    s_path(path, sizeof(path), name, true);
    if (on)
        return storage_write_file(path, (const uint8_t *)"1", 1);
    storage_delete_file(path);   /* absent marker is fine */
    return 0;
}

int pack_store_boot_autorun(void)
{
    if (!lua_engine_is_ready())
        return -623;
    storage_dirent_t ents[PACK_LIST_DIRENTS];
    uint8_t count = 0;
    if (storage_list_dir(PACKS_DIR, ents, PACK_LIST_DIRENTS, &count) != 0)
        return 0;
    char path[STORAGE_MAX_PATH_LEN];
    char err[64];
    int ran = 0;
    for (uint8_t i = 0; i < count; i++) {
        size_t nl = strlen(ents[i].name);
        if (nl <= 4 || strcmp(ents[i].name + nl - 4, PACK_EXT) != 0)
            continue;
        char name[PACK_MAX_NAME + 1];
        if (nl - 4 > PACK_MAX_NAME)
            continue;
        memcpy(name, ents[i].name, nl - 4);
        name[nl - 4] = '\0';
        if (!pack_store_name_ok(name))
            continue;
        s_path(path, sizeof(path), name, true);
        if (storage_file_exists(path) != 1)
            continue;
        if (pack_store_run(name, NULL, 0, err, sizeof(err)) == 0)
            ran++;
        else
            printf("Pack autorun skipped '%s': %s\n", name, err);
    }
    return ran;
}

/* Create the packs directory on first boot (no-op-ish when it exists;
 * host tests stub storage and never call this — see the HOST_BUILD guard). */
int pack_store_init(void)
{
#ifndef HOST_BUILD
    mkdir(PACKS_DIR, 0775);      /* EEXIST is fine */
#else
    (void)0;
#endif
    return 0;
}

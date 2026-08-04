#include "script_if.h"
#include "lua_if.h"
#include "storage_if.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SCRIPT_PATH "/littlefs/script.lua"

/* ---- Upload State ---- */

typedef struct {
    bool     active;
    uint16_t total_received;
    uint32_t last_chunk_tick;
    uint8_t  buffer[SCRIPT_MAX_SIZE];
} script_upload_ctx_t;

static script_upload_ctx_t s_upload = {0};
static bool s_script_loaded = false;
static bool s_script_running = false;
/* L-S3-5 fix: s_upload.buffer doubles as a cache of the last saved script,
 * so script_run() doesn't re-read LittleFS on every call. Invalidated as
 * soon as a new upload starts touching the buffer. */
static bool s_cache_valid = false;

/* ---- Upload API ---- */

int script_upload_begin(void)
{
    if (s_upload.active) {
        return -611;
    }

    s_upload.active = true;
    s_upload.total_received = 0;
    s_upload.last_chunk_tick = xTaskGetTickCount();
    memset(s_upload.buffer, 0, sizeof(s_upload.buffer));
    s_cache_valid = false;  /* buffer is about to be overwritten (L-S3-5 fix) */

    printf("Script: upload begin\n");
    return 0;
}

int script_upload_chunk(const uint8_t *data, uint16_t len)
{
    if (!s_upload.active) {
        return -611;
    }
    if (data == NULL) {
        return -602;
    }

    /* Check timeout — compare in tick domain so the tick→ms multiplication
     * can never overflow (M-S3-5 fix) */
    uint32_t now = xTaskGetTickCount();
    if ((now - s_upload.last_chunk_tick) > pdMS_TO_TICKS(SCRIPT_UPLOAD_TIMEOUT_MS)) {
        s_upload.active = false;
        printf("Script: upload timeout\n");
        return -605;
    }

    /* Check size limit — reserve one byte for the null terminator so
     * upload_end can always terminate in-buffer without truncating the
     * script (L-S3-6 fix: exact-max uploads lost their last byte) */
    if (s_upload.total_received + len > SCRIPT_MAX_SIZE - 1) {
        s_upload.active = false;
        printf("Script: upload exceeds %u bytes\n", (unsigned)(SCRIPT_MAX_SIZE - 1));
        return -603;
    }

    memcpy(s_upload.buffer + s_upload.total_received, data, len);
    s_upload.total_received += len;
    s_upload.last_chunk_tick = now;

    return 0;
}

int script_upload_end(void)
{
    if (!s_upload.active) {
        return -611;
    }

    s_upload.active = false;

    if (s_upload.total_received == 0) {
        printf("Script: empty upload rejected\n");
        return -612;
    }

    /* Null-terminate for Lua — safe because upload_chunk reserves one byte
     * (total_received <= SCRIPT_MAX_SIZE - 1 always holds) */
    s_upload.buffer[s_upload.total_received] = '\0';

    /* Trial compile to catch syntax errors — does NOT execute (B-S3-3 fix) */
    char err_buf[128] = {0};
    int ret = lua_engine_compile_check((const char *)s_upload.buffer, err_buf, sizeof(err_buf));
    if (ret != 0) {
        printf("Script: compile error: %s\n", err_buf);
        return -612;
    }

    /* Save to LittleFS */
    ret = storage_write_file(SCRIPT_PATH, s_upload.buffer, s_upload.total_received);
    if (ret != 0) {
        printf("Script: storage write failed (%d)\n", ret);
        return -704;
    }

    s_script_loaded = true;
    s_cache_valid = true;  /* buffer content == saved file (L-S3-5 fix) */
    printf("Script: saved %u bytes to %s\n", (unsigned)s_upload.total_received, SCRIPT_PATH);
    return 0;
}

int script_run(void)
{
    if (!s_script_loaded) {
        return -611;
    }
    if (s_script_running) {
        return 0; /* already running */
    }

    /* Use the upload buffer as a cache of the saved script (L-S3-5 fix —
     * avoids re-reading LittleFS on every run). The cache is invalidated
     * by script_upload_begin(), so after an interrupted/failed upload we
     * fall back to reading the persisted file.
     * Read into SCRIPT_MAX_SIZE - 1 to always leave room for the
     * terminator (prevents an OOB write at buffer[read_len]). */
    if (!s_cache_valid) {
        uint32_t read_len = 0;
        int ret = storage_read_file(SCRIPT_PATH, s_upload.buffer,
                                    SCRIPT_MAX_SIZE - 1, &read_len);
        if (ret != 0) {
            printf("Script: read failed (%d)\n", ret);
            return ret;
        }
        s_upload.buffer[read_len] = '\0';
        s_cache_valid = true;
    }

    /* Execute the script to define functions */
    char err_buf[128] = {0};
    int ret = lua_engine_exec((const char *)s_upload.buffer, err_buf, sizeof(err_buf));
    if (ret != 0) {
        printf("Script: run failed (%d): %s\n", ret, err_buf);
        return ret;
    }

    s_script_running = true;
    printf("Script: running\n");
    return 0;
}

int script_stop(void)
{
    if (!s_script_running) {
        return 0;
    }

    /* Clear the flag first so the pipeline stops invoking hooks, then remove
     * the hook functions from the Lua global table. Per spec (Script Stop),
     * the compiled script is released from the engine — without this, stale
     * hooks lingered in globals after stop (M-S3-3 fix). */
    s_script_running = false;
    lua_engine_clear_func("on_adv");
    lua_engine_clear_func("transform");

    printf("Script: stopped\n");
    return 0;
}

bool script_is_loaded(void)
{
    return s_script_loaded;
}

bool script_is_running(void)
{
    return s_script_running;
}

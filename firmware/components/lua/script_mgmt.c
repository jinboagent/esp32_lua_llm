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

    /* Check timeout */
    uint32_t now = xTaskGetTickCount();
    uint32_t elapsed_ms = (now - s_upload.last_chunk_tick) * portTICK_PERIOD_MS;
    if (elapsed_ms > SCRIPT_UPLOAD_TIMEOUT_MS) {
        s_upload.active = false;
        printf("Script: upload timeout\n");
        return -605;
    }

    /* Check size limit */
    if (s_upload.total_received + len > SCRIPT_MAX_SIZE) {
        s_upload.active = false;
        printf("Script: upload exceeds %u bytes\n", (unsigned)SCRIPT_MAX_SIZE);
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

    /* Null-terminate for Lua */
    if (s_upload.total_received < SCRIPT_MAX_SIZE) {
        s_upload.buffer[s_upload.total_received] = '\0';
    } else {
        s_upload.buffer[SCRIPT_MAX_SIZE - 1] = '\0';
    }

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

    /* Use the upload buffer for reading — avoids 8KB stack allocation */
    uint32_t read_len = 0;
    int ret = storage_read_file(SCRIPT_PATH, s_upload.buffer, SCRIPT_MAX_SIZE, &read_len);
    if (ret != 0) {
        printf("Script: read failed (%d)\n", ret);
        return ret;
    }
    s_upload.buffer[read_len] = '\0';

    /* Execute the script to define functions */
    char err_buf[128] = {0};
    ret = lua_engine_exec((const char *)s_upload.buffer, err_buf, sizeof(err_buf));
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

    s_script_running = false;
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

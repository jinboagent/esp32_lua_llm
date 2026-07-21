#include "storage_if.h"
#include <string.h>
#include <stdio.h>
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool s_mounted = false;
static SemaphoreHandle_t s_mutex = NULL;

int storage_init(void)
{
    if (s_mounted) {
        return 0;
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        return -701;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        esp_vfs_littlefs_unregister("littlefs");
        return -701;
    }

    s_mounted = true;
    return 0;
}

static int s_check_path(const char *path)
{
    if (path == NULL) return -702;
    if (strlen(path) > STORAGE_MAX_PATH_LEN) return -703;
    return 0;
}

int storage_write_file(const char *path, const uint8_t *data, uint32_t len)
{
    int ret = s_check_path(path);
    if (ret != 0) return ret;
    if (data == NULL) return -702;
    if (len > STORAGE_MAX_SCRIPT_SIZE) return -704;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -700;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        xSemaphoreGive(s_mutex);
        return -705;
    }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        remove(path);
        xSemaphoreGive(s_mutex);
        return -705;
    }
    xSemaphoreGive(s_mutex);
    return 0;
}

int storage_read_file(const char *path, uint8_t *buf, uint32_t buf_len, uint32_t *out_len)
{
    int ret = s_check_path(path);
    if (ret != 0) return ret;
    if (buf == NULL || out_len == NULL) return -702;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -700;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        xSemaphoreGive(s_mutex);
        return -706;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if ((uint32_t)file_size > buf_len) {
        fclose(f);
        xSemaphoreGive(s_mutex);
        return -707;
    }

    size_t read = fread(buf, 1, file_size, f);
    fclose(f);
    xSemaphoreGive(s_mutex);

    *out_len = (uint32_t)read;
    return 0;
}

int storage_delete_file(const char *path)
{
    int ret = s_check_path(path);
    if (ret != 0) return ret;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -700;
    }

    int err = remove(path);
    xSemaphoreGive(s_mutex);

    if (err != 0) {
        return -706;
    }
    return 0;
}

int storage_file_exists(const char *path)
{
    int ret = s_check_path(path);
    if (ret != 0) return ret;

    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

int storage_get_free_space(uint32_t *free_bytes)
{
    if (free_bytes == NULL) return -702;

    size_t total = 0, used = 0;
    esp_err_t err = esp_littlefs_info("littlefs", &total, &used);
    if (err != ESP_OK) {
        *free_bytes = 0;
        return -700;
    }

    *free_bytes = (uint32_t)(total - used);
    return 0;
}

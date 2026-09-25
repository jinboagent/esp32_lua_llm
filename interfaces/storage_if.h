#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LittleFS Storage Interface
 *
 * Persistent file storage using LittleFS filesystem.
 * 64KB partition, max 8KB per script file.
 *
 * Error codes (module range -700 to -799):
 *   0        Success
 *   -700     General failure
 *   -701     Mount failure
 *   -702     NULL parameter or invalid argument
 *   -703     Path too long (>128 chars)
 *   -704     File too large (>8KB)
 *   -705     Filesystem full or write failed
 *   -706     File not found
 *   -707     Buffer too small for read
 */

#define STORAGE_MAX_PATH_LEN    128
#define STORAGE_MAX_SCRIPT_SIZE 8192   /* 8KB */
#define STORAGE_PARTITION_SIZE  65536  /* 64KB */
#define STORAGE_MAX_NAME_LEN    32     /* dirent name buffer (H6.1 M2) */

/* One directory entry (H6.1 M2 pack listing). */
typedef struct {
    char     name[STORAGE_MAX_NAME_LEN];  /* file name, no directory */
    uint32_t size;                        /* file size in bytes */
} storage_dirent_t;

int storage_init(void);
int storage_write_file(const char *path, const uint8_t *data, uint32_t len);
int storage_read_file(const char *path, uint8_t *buf, uint32_t buf_len, uint32_t *out_len);
int storage_delete_file(const char *path);
int storage_file_exists(const char *path);
int storage_get_free_space(uint32_t *free_bytes);

/*
 * List a directory's files (H6.1 M2). Fills entries up to max_entries;
 * *count receives the number written. "." and ".." are skipped.
 * @return 0, -702 NULL/invalid, -706 directory not found; an entry
 *         whose name does not fit STORAGE_MAX_NAME_LEN is skipped.
 */
int storage_list_dir(const char *path, storage_dirent_t *entries,
                     uint8_t max_entries, uint8_t *count);

#ifdef __cplusplus
}
#endif

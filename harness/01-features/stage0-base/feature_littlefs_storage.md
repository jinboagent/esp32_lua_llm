# Feature: LittleFS Storage

**Feature ID:** F0.2  
**Stage:** 0 (Base Infrastructure)  
**Layer:** HAL (Hardware Abstraction Layer)  
**Dependencies:** None  
**Status:** Defined  

## Overview

Mount and manage LittleFS filesystem for storing Lua scripts and configuration files. Provides file read/write/delete operations with size limits and space management.

## Source Files

- **Interface:** `interfaces/storage_if.h`
- **Implementation:** `firmware/components/storage/littlefs_storage.c`

## Test Files

- **Target tests:** `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)`

## Description

LittleFS Storage provides persistent file storage using the LittleFS filesystem. It manages a 64KB partition for storing Lua scripts (max 8KB each) and other configuration files. The module handles filesystem mounting, file operations, and space tracking.

## Public API

```c
int storage_init(void);
int storage_write_file(const char *path, const uint8_t *data, uint32_t len);
int storage_read_file(const char *path, uint8_t *buf, uint32_t buf_len, uint32_t *out_len);
int storage_delete_file(const char *path);
int storage_file_exists(const char *path);
int storage_get_free_space(uint32_t *free_bytes);
```

### API Details

- **storage_init()**: Mount LittleFS filesystem. Returns 0 on success, -701 on mount failure, -702 on NULL parameter.
- **storage_write_file(const char *path, const uint8_t *data, uint32_t len)**: Write data to file. Creates or overwrites. Returns 0 on success, -702 if NULL parameter, -703 if path too long (>128 chars), -704 if file too large (>8KB), -705 if filesystem full.
- **storage_read_file(const char *path, uint8_t *buf, uint32_t buf_len, uint32_t *out_len)**: Read file into buffer. Returns 0 on success, -702 if NULL parameter, -703 if path too long, -706 if file not found, -707 if buffer too small.
- **storage_delete_file(const char *path)**: Delete file. Returns 0 on success, -702 if NULL parameter, -703 if path too long, -706 if file not found.
- **storage_file_exists(const char *path)**: Check if file exists. Returns 1 if exists, 0 if not, -702 if NULL parameter, -703 if path too long.
- **storage_get_free_space(uint32_t *free_bytes)**: Get available space. Returns 0 on success, -702 if NULL parameter.

## Data Structures

```c
// Configuration
#define STORAGE_MAX_PATH_LEN    128
#define STORAGE_MAX_SCRIPT_SIZE 8192  // 8KB
#define STORAGE_PARTITION_SIZE  65536 // 64KB

// Internal state
static bool storage_mounted = false;
static osMutexId_t storage_mutex = NULL;
```

## File System Layout

<!-- chart-id: CH-lfs-md-01 rev1 -->
```
/littlefs/
├── script.lua                 # F3.2 script slot (SCRIPT LOAD)
├── packs/                     # H6.1 tool packs + .autorun markers
│   ├── demo.lua
│   └── demo.autorun
├── kv/                        # hw.kv_set / hw.kv_get persistent store
└── config/
    └── settings.json
```

## Error Codes

- **0:** Success
- **-701:** Mount failure
- **-702:** NULL parameter or invalid argument
- **-703:** Path too long (>128 chars)
- **-704:** File too large (>8KB for scripts)
- **-705:** Filesystem full
- **-706:** File not found
- **-707:** Buffer too small for read

## Acceptance Criteria

- [ ] `storage_init()` successfully mounts LittleFS filesystem
- [ ] Write then read returns identical data
- [ ] Delete removes file from filesystem
- [ ] Write operation rejects files >8KB
- [ ] Write operation returns error when filesystem full
- [ ] Read non-existent file returns -706
- [ ] Delete non-existent file returns -706
- [ ] Path length >128 chars returns -703
- [ ] NULL parameters return -702

## Test Cases

### TC-1: Init/mount success
- **Action:** Call `storage_init()`
- **Expected:** Returns 0, filesystem mounted

### TC-2: Write and read back
- **Action:** Write "test data" to "/test.txt", then read back
- **Expected:** Returns 0, data matches

### TC-3: Read non-existent file
- **Action:** Call `storage_read_file("/nofile.txt", ...)`
- **Expected:** Returns -706

### TC-4: Delete existing file
- **Action:** Write "/test.txt", then delete it
- **Expected:** Returns 0, file no longer exists

### TC-5: Delete non-existent file
- **Action:** Call `storage_delete_file("/nofile.txt")`
- **Expected:** Returns -706

### TC-6: Write >8KB script rejected
- **Action:** Attempt to write 9KB data to "/scripts/big.lua"
- **Expected:** Returns -704

### TC-7: Filesystem full
- **Action:** Fill filesystem with files until full, attempt another write
- **Expected:** Returns -705

### TC-8: NULL params
- **Action:** Call `storage_write_file(NULL, data, len)`
- **Expected:** Returns -702
- **Action:** Call `storage_read_file(path, NULL, len, out_len)`
- **Expected:** Returns -702

## Non-Functional Requirements

- **No dynamic allocation:** All paths statically allocated (max 128 chars)
- **Thread safety:** All operations protected by mutex
- **Wear leveling:** LittleFS provides dynamic wear leveling
- **Power safe:** LittleFS is power-loss resilient

## Constraints

- **Partition size:** 64KB fixed
- **Max script size:** 8KB per file
- **Max path length:** 128 characters
- **File system:** LittleFS (not SPIFFS or FAT)

## Integration Notes

- LittleFS partition must be defined in partition table
- This module provides raw file operations
- Lua script management built on top of this module
- Configuration files stored in /config/ directory
- File paths must be absolute (start with /)

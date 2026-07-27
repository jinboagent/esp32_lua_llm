#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Script Management Interface — Upload, persist, run Lua scripts with pipeline hooks
 *
 * Error codes:
 *   0     Success
 *   -602  NULL pointer
 *   -603  Script exceeds 8 KB maximum
 *   -605  Upload timeout (>5s between chunks)
 *   -606  Not initialized
 *   -611  Invalid state (e.g., chunk without begin)
 *   -612  Script compile error
 *   -613  Script runtime error (hook failed)
 *   -704  LittleFS write failed
 */

#define SCRIPT_MAX_SIZE      8192
#define SCRIPT_UPLOAD_TIMEOUT_MS  5000

/*
 * Begin a new script upload session.
 * Must call upload_chunk one or more times, then upload_end.
 */
int script_upload_begin(void);

/*
 * Upload a chunk of script source code.
 * @param data  Chunk data (not copied — must remain valid until next call).
 * @param len   Chunk length in bytes (max 512 recommended).
 */
int script_upload_chunk(const uint8_t *data, uint16_t len);

/*
 * Finalize upload: trial-compile, save to LittleFS as /script.lua.
 * Returns -612 on compile error (script NOT saved).
 */
int script_upload_end(void);

/*
 * Load the saved script into the Lua engine and activate hooks.
 * Returns -611 if no script is uploaded.
 */
int script_run(void);

/*
 * Stop the running script; pipeline reverts to default behavior.
 */
int script_stop(void);

/* Returns true if a script is compiled and loaded in the Lua engine. */
bool script_is_loaded(void);

/* Returns true if hooks are actively being called. */
bool script_is_running(void);

#ifdef __cplusplus
}
#endif

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
/* Inter-chunk timeout. Raised from 5 s to 30 s for the F4.2 text-line
 * upload protocol, where the host (LLM) may pause between lines. */
#define SCRIPT_UPLOAD_TIMEOUT_MS  30000

/*
 * Begin a new script upload session.
 * Must call upload_chunk one or more times, then upload_end.
 */
int script_upload_begin(void);

/*
 * Upload a chunk of script source code.
 * @param data  Chunk data (not copied — must remain valid until next call).
 * @param len   Chunk length in bytes. Engine-side the total script may be
 *              up to SCRIPT_MAX_SIZE, but note the TRANSPORT limit: over
 *              USB CDC a command line fits 255 chars (USB_RX_BUFFER_SIZE
 *              256), so a "SCRIPT CHUNK <hex>" payload is capped at
 *              121 bytes per chunk. Larger chunks overflow the console
 *              line and are rejected (-504). (H3 doc fix 2026-08-11)
 */
int script_upload_chunk(const uint8_t *data, uint16_t len);

/*
 * Finalize upload: trial-compile, save to LittleFS as /script.lua.
 * Returns -612 on compile error (script NOT saved).
 *
 * @param err_buf   Optional buffer receiving the compile error message.
 * @param err_len   Size of err_buf (0 when err_buf is NULL).
 */
int script_upload_end(char *err_buf, uint16_t err_len);

/*
 * Abort an in-progress upload: deactivate the session and invalidate
 * the buffer cache (a later script_run falls back to the persisted
 * file). No-op when no upload is active.
 */
int script_upload_abort(void);

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

/* Hook-presence cache (refreshed at SCRIPT RUN/STOP) so the pipeline can
 * skip Lua lock round-trips per advertisement. */
bool script_has_on_adv(void);
bool script_has_transform(void);

#ifdef __cplusplus
}
#endif

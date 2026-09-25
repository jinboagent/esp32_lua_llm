#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LLM Script Bridge Interface (F4.2)
 *
 * Protocol layer for receiving LLM-generated Lua scripts from the host
 * PC as plain text lines over USB CDC:
 *
 *   PC                            Device
 *   |-- SCRIPT LOAD ------------->|  bridge_upload_begin  -> "ready"
 *   |-- script line 1 ----------->|  bridge_handle_script_upload (silent)
 *   |-- script line 2 ----------->|  ...
 *   |-- SCRIPT END -------------->|  bridge_upload_finish -> "saved"/error
 *
 * The bridge owns the protocol state (upload mode, per-line sandbox
 * scan, size accounting) and delegates buffering, trial-compile and
 * persistence to script_mgmt, so the text-line and hex-chunk upload
 * paths converge on one validated deployment path.
 *
 * Sandbox scan (AC #7): every received line is scanned for forbidden
 * tokens (os.*, io.*, debug.*, package.*, dofile, loadfile, load,
 * require, collectgarbage). A match aborts the upload with -612.
 * Detection is fail-closed: a token inside a string literal or comment
 * also rejects the script.
 *
 * Error codes (module range):
 *   0      Success
 *   -611   Invalid state (no upload in progress)
 *   -612   Script rejected: sandbox violation, empty script, compile error
 *   -704   Storage write failed
 *   -802   NULL pointer
 *   -803   Script exceeds the 8 KB buffer
 */

#define BRIDGE_ERR_STATE      (-611)
#define BRIDGE_ERR_REJECTED   (-612)
#define BRIDGE_ERR_STORAGE    (-704)
#define BRIDGE_ERR_NULL       (-802)
#define BRIDGE_ERR_TOO_BIG    (-803)

typedef enum {
    BRIDGE_STATE_IDLE = 0,
    BRIDGE_STATE_UPLOADING,
    BRIDGE_STATE_VALIDATED,
    BRIDGE_STATE_RUNNING
} bridge_state_t;

typedef struct {
    int         error_code;     /* 0 = success, negative = error           */
    const char *error_msg;      /* Human-readable error (NULL on success)  */
    uint32_t    script_size;    /* Bytes accumulated                       */
} bridge_upload_result_t;

/*
 * Initialize the bridge subsystem. Resets upload state to IDLE.
 * @return 0 on success, negative error code on failure.
 */
int bridge_init(void);

/*
 * Handle one line of script text during an upload.
 *
 * Scans the line for sandbox-violating tokens, checks the 8 KB budget
 * and forwards it (plus a newline) to the script upload buffer.
 * Silent on success: writes an empty response (the protocol answers
 * only SCRIPT LOAD / SCRIPT END).
 *
 * @param script_text  Line text (no trailing newline required).
 * @param len          Length in bytes.
 * @param response     Caller-allocated response buffer.
 * @param response_len Size of response in bytes.
 * @return 0 on success, -802 NULL, -611 not uploading, -612 sandbox
 *         violation, -803 buffer full. On error the upload is aborted.
 */
int bridge_handle_script_upload(const char *script_text, uint32_t len,
                                char *response, uint16_t response_len);

/*
 * SCRIPT LOAD — enter upload mode. Fails if an upload is already
 * active (text or hex). Writes {"status":"ok","cmd":"script_load",
 * "msg":"ready"} on success.
 */
int bridge_upload_begin(char *response, uint16_t response_len);

/*
 * SCRIPT END — validate (empty check + trial compile) and deploy.
 * Writes {"status":"ok","cmd":"script_end","size":N} on success or an
 * error JSON carrying the compile/storage message on failure.
 */
int bridge_upload_finish(char *response, uint16_t response_len);

/*
 * Abort an in-progress upload and reset the buffer. Safe when idle.
 * Called by the CLI when a non-script command arrives mid-upload (AC #4).
 */
void bridge_abort(void);

/* True while a text-line upload is in progress. */
bool bridge_is_uploading(void);

/*
 * Scan one Lua line for forbidden sandbox tokens (the AC #7 list).
 * Returns the offending token, or NULL when clean. Public so pack_store
 * (H6.1 M2) applies the identical fail-closed policy to tool packs.
 */
const char *bridge_scan_line(const char *line, size_t len);

/* Current bridge state (RUNNING reflects the script subsystem). */
bridge_state_t bridge_get_state(void);

#ifdef __cplusplus
}
#endif

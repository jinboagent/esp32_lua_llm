#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pack Store Interface (H6.1 M2 — multi-pack storage + boot autorun)
 *
 * Named Lua tool packs persisted under /littlefs/packs/:
 *   <name>.lua      the pack text (uploaded line-by-line, fail-closed
 *                   scanned with the bridge token list, compile-checked)
 *   <name>.autorun  empty marker: firmware executes the pack at boot
 *                   (decision 13's reserved seam, honored by M2)
 *
 * Packs live OUTSIDE the CLI state machine (decision 11): upload and
 * PACK RUN are valid while scanning or while a filter script runs —
 * the engine lock serializes execution. A pack upload never touches
 * the single filter-script slot.
 *
 * Error codes (module range -620 to -629; storage codes pass through):
 *   0      Success
 *   -611   Invalid state (e.g. no upload in progress)
 *   -612   Rejected: sandbox violation, compile error, empty pack
 *   -620   Invalid pack name (must be [a-zA-Z0-9_], <= 24 chars)
 *   -621   Pack not found
 *   -622   Pack too large (> 8 KB)
 *   -623   Lua engine not ready
 */

#define PACK_MAX_NAME   24
#define PACK_MAX_FILES  8
#define PACK_MAX_SIZE   8192

/* True while a PACK BEGIN ... PACK END upload is in progress. */
bool pack_store_is_uploading(void);

/*
 * Create the packs directory (idempotent; called from main after
 * storage_init). Returns 0.
 */
int pack_store_init(void);

/* Abort an in-progress pack upload. Safe when idle. */
void pack_store_abort(void);

/* Validate a pack name (see error -620). */
bool pack_store_name_ok(const char *name);

/*
 * PACK BEGIN <name> [autorun] — open an upload session for <name>.
 * autorun=true writes the boot marker at upload finish.
 * @return 0, or -620/-611; err receives a human-readable reason.
 */
int pack_store_upload_begin(const char *name, bool autorun,
                            char *err, uint16_t err_len);

/*
 * One pack text line during an upload. Scanned with the bridge token
 * list (fail-closed, -612), appended with '\n'. On error the session
 * is aborted.
 */
int pack_store_upload_line(const char *line, uint32_t len,
                           const char **reject_token);

/*
 * PACK END — compile-check the accumulated text, persist it under
 * /littlefs/packs/<name>.lua (+ the autorun marker when requested;
 * a stale marker is cleared on overwrite), close the session.
 * @param out_size  stored size in bytes (may be NULL).
 */
int pack_store_upload_finish(uint32_t *out_size, char *err, uint16_t err_len);

/*
 * PACK RUN <name> — execute a stored pack NOW in the live Lua state
 * (defines its globals; tools become callable). Returns the exec
 * result string like LUA EXEC (usually empty for packs).
 */
int pack_store_run(const char *name, char *result, uint16_t result_len,
                   char *err, uint16_t err_len);

/* Build the full pack_list JSON response (status/cmd/packs/free). */
int pack_store_list(char *json, uint16_t json_len);

/* PACK DEL <name> — remove the pack and any autorun marker. */
int pack_store_del(const char *name);

/* PACK AUTORUN <name> ON|OFF — set/clear the boot marker. */
int pack_store_set_autorun(const char *name, bool on);

/*
 * Boot hook (main.c, after lua_engine_init): execute every pack that
 * carries an autorun marker, so tools are alive before any host
 * connects. @return packs executed, or a negative error.
 */
int pack_store_boot_autorun(void);

#ifdef __cplusplus
}
#endif

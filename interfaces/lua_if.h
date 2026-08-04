#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "proto_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lua Engine Interface — Sandboxed Lua 5.4 VM with memory limits
 *
 * Error codes (module range -600 to -619):
 *   0        Success
 *   -600     General failure
 *   -602     NULL pointer
 *   -606     Engine not initialized
 *   -607     Already initialized
 *   -610     Out of memory (allocator limit reached)
 *   -612     Script compile error (syntax error)
 *   -613     Script runtime error
 *   -614     Execution timeout (instruction limit exceeded)
 */

/* Memory limit for Lua allocator (128KB with PSRAM, 32KB without) */
#define LUA_MEMORY_LIMIT       (128 * 1024)

/* Maximum result buffer size from lua_engine_exec */
#define LUA_RESULT_MAX_LEN     256

/* Execution timeout: the debug hook fires every 1000 instructions, so
 * 100 hook calls ≈ 100k instructions ≈ 10 ms on ESP32-S3 @ 160 MHz
 * (M-S3-6 fix: hook interval was 100, spec requires 1000) */
#define LUA_EXEC_TIMEOUT_INSTR 100

/*
 * Initialize the Lua engine.
 *
 * Creates a Lua VM with a custom memory-limited allocator and sandboxed
 * standard library (string, table, math, utf8 only). Removes os, io,
 * debug, package, coroutine, loadfile, dofile.
 *
 * @return 0 on success, -607 if already initialized, -600 on alloc failure.
 */
int lua_engine_init(void);

/*
 * Destroy the Lua engine and free all memory.
 *
 * @return 0 on success, -606 if not initialized.
 */
int lua_engine_deinit(void);

/*
 * Compile and execute a Lua script.
 *
 * The script runs in the sandboxed environment. If the script returns
 * a value, it is converted to a string and written to result.
 * An instruction-count hook enforces LUA_EXEC_TIMEOUT_INSTR limit.
 *
 * @param script      Lua source code (null-terminated). Must not be NULL.
 * @param result      Output buffer for return value (may be NULL if not needed).
 * @param result_len  Size of result buffer.
 * @return 0 on success, negative error code on failure:
 *         -602 NULL script pointer
 *         -606 engine not initialized
 *         -610 out of memory during execution
 *         -612 compile error (syntax)
 *         -613 runtime error
 *         -614 execution timeout
 */
int lua_engine_exec(const char *script, char *result, uint16_t result_len);

/*
 * Check if the Lua engine is initialized and ready.
 *
 * @return true if engine is initialized, false otherwise.
 */
bool lua_engine_is_ready(void);

/*
 * Check if a Lua global function exists.
 *
 * @param name  Function name (null-terminated).
 * @return 1 if function exists, 0 if not, -606 if engine not ready.
 */
int lua_engine_has_func(const char *name);

/*
 * Call a Lua global function with the spec hook signature:
 *   on_adv(addr, addr_type, rssi, name, uuids, manu_id, manu_data) → bool
 * Used for the on_adv hook. All arguments are taken from the parsed report.
 *
 * @param func_name  Lua function name (e.g., "on_adv").
 * @param report     Parsed advertisement report (addr, addr_type, rssi,
 *                   name, uuid16 list, manufacturer id/data). Must not be NULL.
 * @return 1 = pass (true), 0 = suppress (false), -1 = function not defined,
 *         negative error code on failure:
 *         -602 NULL pointer, -606 engine not ready,
 *         -610 out of memory, -613 runtime error, -614 timeout.
 */
int lua_engine_call_on_adv(const char *func_name, const proto_adv_report_t *report);

/*
 * Call a Lua global function: func(addr, json) → string
 * Used for the transform hook.
 *
 * @param func_name  Lua function name (e.g., "transform").
 * @param addr       BLE address string.
 * @param json_in    Default JSON string from encoder.
 * @param json_out   Output buffer for transformed JSON.
 * @param out_len    Size of output buffer.
 * @return 0 on success, -1 = function not defined, negative error code on failure.
 */
int lua_engine_call_transform(const char *func_name, const char *addr,
                              const char *json_in, char *json_out, uint16_t out_len);

/*
 * Trial-compile a Lua script without executing it.
 * Used to verify syntax before saving to storage.
 *
 * @param script   Lua source code (null-terminated).
 * @param err      Output buffer for error message (may be NULL).
 * @param err_len  Size of error buffer.
 * @return 0 on success, -612 on compile error (error message in err).
 */
int lua_engine_compile_check(const char *script, char *err, uint16_t err_len);

/*
 * Remove a global function from the Lua state (sets it to nil).
 * Used by script_stop() to release hook functions so a stopped script
 * cannot linger in the global table.
 *
 * @param name  Global name to clear (e.g., "on_adv", "transform").
 * @return 0 on success, -602 if NULL, -606 if engine not ready.
 */
int lua_engine_clear_func(const char *name);

/*
 * Acquire the Lua engine mutex. Must be called before accessing
 * the Lua VM from a task other than the one that called lua_engine_exec.
 * Use lua_engine_unlock() when done.
 */
void lua_engine_lock(void);

/* Release the Lua engine mutex. */
void lua_engine_unlock(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

/* Execution timeout in hook calls (hook fires every 100 instructions, so 100 calls = ~10ms on ESP32-S3 @ 160MHz) */
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
 * Call a Lua global function: func(addr, rssi, name) → bool
 * Used for the on_adv hook.
 *
 * @param func_name  Lua function name (e.g., "on_adv").
 * @param addr       BLE address as "AA:BB:CC:DD:EE:FF".
 * @param rssi       RSSI in dBm.
 * @param name       Device name (may be NULL).
 * @return 1 = pass (true), 0 = suppress (false), -1 = function not defined,
 *         negative error code on failure.
 */
int lua_engine_call_on_adv(const char *func_name, const char *addr,
                           int8_t rssi, const char *name);

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

#ifdef __cplusplus
}
#endif

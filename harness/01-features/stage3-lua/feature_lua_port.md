# Feature: Lua 5.4 Interpreter Port

| Field | Value |
|-------|-------|
| **Feature ID** | F3.1 |
| **Stage** | 3 — Lua |
| **Layer** | Lua |
| **Dependencies** | F0.2 (LittleFS storage) |
| **Source Files** | `interfaces/lua_if.h`, `firmware/components/lua/lua_port.c` |
| **Test File** | `tests/host/ (Unity host suite) + root Python HIL suites (see README Test strategy)` |

---

## 1. Description

Integrate the Lua 5.4 interpreter on the ESP32-S3 with a memory-limited custom allocator and a sandboxed standard library set. The Lua VM runs as a single `lua_State` owned by the Lua task. Scripts are executed via `lua_engine_exec()`, which compiles and runs the script in a controlled environment with a hard execution timeout.

The custom allocator enforces a strict memory ceiling to prevent the Lua VM from exhausting system RAM. The sandbox restricts available libraries to a safe subset, blocking filesystem, OS, and debug access.

---

## 2. Public API

| Function | Signature | Description |
|----------|-----------|-------------|
| `lua_engine_init` | `int lua_engine_init(void)` | Create Lua VM with custom allocator and sandbox. Returns 0 on success. |
| `lua_engine_deinit` | `int lua_engine_deinit(void)` | Destroy Lua VM and release all Lua memory. Returns 0 on success. |
| `lua_engine_exec` | `int lua_engine_exec(const char *script, char *result, uint16_t result_len)` | Compile and execute a Lua script. Output is written to `result` (up to `result_len` bytes). |
| `lua_engine_is_running` | `bool lua_engine_is_running(void)` | Returns `true` if a script is currently executing. |

### Return / Error Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `-602` | NULL pointer passed to a required parameter |
| `-606` | Lua engine not initialized (`lua_engine_init` not called) |
| `-610` | Out of memory — Lua allocator limit reached |
| `-612` | Script compile error (syntax error) |
| `-613` | Script runtime error |
| `-614` | Execution timeout (instruction limit exceeded) |

---

## 3. Lua VM Configuration

| Parameter | Value (no PSRAM) | Value (with PSRAM) | Notes |
|-----------|-------------------|---------------------|-------|
| Memory limit | **32 KB** | **128 KB** | Enforced by custom allocator |
| Allowed libraries | `string`, `table`, `math`, `utf8` | Same | Sandboxed subset |
| Denied libraries | `os`, `io`, `debug`, `package` | Same | Removed after `luaL_openlibs` |
| Denied functions | `loadfile`, `dofile`, `coroutine.*` | Same | Explicitly removed from globals |
| Coroutines | Disabled | Disabled | `coroutine` table removed |
| Execution timeout | **10 ms** per `lua_engine_exec` call | Same | Enforced via debug hook |

---

## 4. Internal Behavior

### Custom Allocator

```c
static void *lua_custom_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    lua_alloc_ctx_t *ctx = (lua_alloc_ctx_t *)ud;
    if (nsize == 0) {
        /* Free: return memory to pool */
        ctx->used -= osize;
        pool_free(ptr);
        return NULL;
    }
    if (ptr == NULL) {
        /* New allocation */
        if (ctx->used + nsize > ctx->limit) return NULL;  /* OOM */
        void *p = pool_alloc(nsize);
        if (p) ctx->used += nsize;
        return p;
    }
    /* Resize */
    if (ctx->used - osize + nsize > ctx->limit) return NULL;  /* OOM */
    void *p = pool_realloc(ptr, osize, nsize);
    if (p) ctx->used = ctx->used - osize + nsize;
    return p;
}
```

- The allocator uses a static memory pool (no `malloc`/`free`).
- Pool size is 32 KB (no PSRAM) or 128 KB (with PSRAM).
- When the limit is reached, the allocator returns NULL, which causes Lua to raise an "out of memory" error.

### Sandbox Setup

After `luaL_openlibs(L)`, the following is performed:

1. Remove `os`, `io`, `debug`, `package` tables from the global environment.
2. Remove `loadfile`, `dofile` from globals.
3. Remove the `coroutine` table from globals.
4. Set `package.path` and `package.cpath` to empty strings (belt-and-suspenders).

### Execution Timeout

- A debug hook is installed via `lua_sethook(L, hook, LUA_MASKCOUNT, 1000)`.
- The hook increments an instruction counter.
- If the counter exceeds the limit corresponding to 10 ms of execution (calibrated at init), the hook calls `luaL_error(L, "execution timeout")`.
- This causes `lua_pcall` to return `LUA_ERRRUN` with the timeout message, which is mapped to error code `-614`.

### Memory Model

- No `malloc`/`free` in application code. Lua memory is served from a static pool.
- The Lua VM state (`lua_State`) is allocated from the same pool.
- All pool memory is freed on `lua_engine_deinit`.

---

## 5. Acceptance Criteria

| # | Criterion |
|---|-----------|
| AC-1 | `lua_engine_init()` returns `0` and creates a Lua VM with the memory limit enforced. |
| AC-2 | `lua_engine_exec("return 1+1", buf, sizeof(buf))` writes `"2"` to `buf` and returns `0`. |
| AC-3 | Sandbox blocks `os.execute("ls")` — returns `-613` (runtime error). |
| AC-4 | Sandbox blocks `io.open("/etc/passwd")` — returns `-613`. |
| AC-5 | An infinite loop (`while true do end`) triggers the timeout and returns `-614`. |
| AC-6 | Allocating more than the memory limit returns `-610`. |
| AC-7 | A script with a syntax error returns `-612`. |
| AC-8 | `lua_engine_deinit()` returns `0` and all Lua memory is freed. |

---

## 6. Test Cases

| ID | Name | Setup | Action | Expected Result |
|----|------|-------|--------|-----------------|
| TC-1 | Init success | Engine not initialized | `lua_engine_init()` | Returns `0` |
| TC-2 | Deinit | Engine initialized | `lua_engine_deinit()` | Returns `0` |
| TC-3 | Exec simple script | Engine initialized | `lua_engine_exec("return 42", buf, 64)` | Returns `0`, buf contains `"42"` |
| TC-4 | Exec with return value | Engine initialized | `lua_engine_exec("return 'hello'", buf, 64)` | Returns `0`, buf contains `"hello"` |
| TC-5 | Sandbox blocks os.execute | Engine initialized | `lua_engine_exec("os.execute('ls')", buf, 64)` | Returns `-613` |
| TC-6 | Sandbox blocks io.open | Engine initialized | `lua_engine_exec("io.open('/tmp/x')", buf, 64)` | Returns `-613` |
| TC-7 | Infinite loop timeout | Engine initialized | `lua_engine_exec("while true do end", buf, 64)` | Returns `-614` |
| TC-8 | Memory limit enforced | Engine initialized | Execute script that allocates large tables until OOM | Returns `-610` |
| TC-9 | Compile error | Engine initialized | `lua_engine_exec("return +++", buf, 64)` | Returns `-612` |
| TC-10 | Runtime error | Engine initialized | `lua_engine_exec("error('boom')", buf, 64)` | Returns `-613` |
| TC-11 | NULL params | Engine initialized | `lua_engine_exec(NULL, buf, 64)` | Returns `-602` |
| TC-12 | Double init | Engine initialized | `lua_engine_init()` | Returns error (already initialized) |

---

## 7. Non-Functional Requirements

| Requirement | Constraint |
|-------------|------------|
| RAM usage | Lua VM uses at most **32 KB** (no PSRAM) or **128 KB** (with PSRAM) |
| Init latency | `lua_engine_init()` must complete within **100 ms** |
| Memory isolation | Lua allocator cannot access memory outside its pool |
| Determinism | No `malloc`/`free` in application code; Lua uses static pool |
| Fault isolation | A script error does not crash the firmware; it returns an error code |

---

## 8. Interaction with Other Features

| Feature | Relationship |
|---------|-------------|
| F3.2 (Lua script management) | Script management uses the Lua engine to compile and run uploaded scripts |
| F0.2 (LittleFS) | Scripts are stored on LittleFS; the Lua engine reads them via the storage layer |
| F2.3 (Scan pipeline) | Future: pipeline calls Lua hooks (`on_adv`, `transform`) via `lua_engine_exec` |

---

## 9. Open Questions

- Should the instruction-count threshold for the 10 ms timeout be calibrated at runtime or set at compile time?
- Should `lua_engine_exec` support returning structured data (tables) or only string/number results?
- Is 32 KB sufficient for useful filter/transform scripts, or should the no-PSRAM limit be increased?

## Implementation Notes (v1.0.0, 2026-08-16)
- API renamed `lua_engine_is_ready()` (spec: `lua_engine_is_running`).
- Whitelist loading is stronger than the spec's openlibs-then-remove.
- Pool fixed at 128 KB; CPU cap is a fixed instruction budget, not calibrated ms.
- Pool allocator rewritten 2026-08-16 (lua_pool.c: uint32 offsets, on-free coalescing, single ledger) per docs/evaluation-response-2026-08-16.md.
- Upload token scan is fail-closed (rejects tokens even in comments/strings) - UX cost by design; the library whitelist is the security boundary.
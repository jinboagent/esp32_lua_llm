# lua_alt — Registry-Based Lua Alternative

Switchable alternative to the vendored Lua in `firmware/components/lua/`.

## The Two Implementations

| Mode | Lua source | Component | Wrapper compiled by |
|------|-----------|-----------|---------------------|
| `vendored` (default) | Upstream Lua 5.4.8 from lua.org, committed in git | `firmware/components/lua/` | `firmware/components/lua/` |
| `registry` | UncleRus/esp-idf-lua, downloaded from ESP-IDF registry | `managed_components/uncleRus__lua/` | `firmware/components/lua_alt/` |

Both expose the same API: `lua_engine_*` (see `interfaces/lua_if.h`) and
`script_*` (see `interfaces/script_if.h`). Consumers (`main`, `ble`) use a
conditional `REQUIRES` selected by the top-level `LUA_SOURCE` cache variable.

## Switching

```
# Default (vendored, no network needed):
idf.py build

# Registry mode (needs network + manifest enabled):
rename idf_component.yml.disabled idf_component.yml
idf.py -D LUA_SOURCE=registry build

# Back to vendored:
rename idf_component.yml idf_component.yml.disabled
idf.py build        # delete build/ if cmake cache keeps old LUA_SOURCE
```

## Why the manifest ships disabled

The component manager may resolve manifests of all project components at
configure time. Keeping it `.disabled` guarantees default builds work offline.
This machine currently has DNS issues to external hosts, so registry mode has
NOT been build-tested yet — the vendored path remains the verified default.

## Known risks (registry mode)

- Exact registry namespace/name unverified (expected `uncleRus/lua`) —
  confirm via `idf.py add-dependency uncleRus/lua` or registry web search.
- UncleRus package pins its own Lua version/build flags; if its `luaconf.h`
  differs materially from upstream, our `lua_port.c` pool allocator and
  whitelist sandbox may need adjustments.
- Package is stale (last upstream activity 2023).

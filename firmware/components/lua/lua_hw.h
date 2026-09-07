#pragma once

/* H6.1 M3: optional hw.* bindings for the Lua sandbox. Declared here
 * (engine-internal, beside lua_port.c) — registration is a no-op when
 * CONFIG_LUA_HW_BINDINGS is off. */
struct lua_State;
void lua_hw_register(struct lua_State *L);

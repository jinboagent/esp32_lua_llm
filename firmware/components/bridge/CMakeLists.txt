# Select Lua implementation component based on LUA_SOURCE (top-level cache var)
if(LUA_SOURCE STREQUAL "registry")
    set(_lua_dep lua_alt)
else()
    set(_lua_dep lua)
endif()

idf_component_register(
    SRCS "lua_llm_bridge.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES json_enc ${_lua_dep}
)

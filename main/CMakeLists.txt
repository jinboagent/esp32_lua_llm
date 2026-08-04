# Select Lua implementation component based on LUA_SOURCE (top-level cache var)
if(LUA_SOURCE STREQUAL "registry")
    set(_lua_dep lua_alt)
else()
    set(_lua_dep lua)
endif()

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES proto json_enc filter usb storage ble ${_lua_dep}
)

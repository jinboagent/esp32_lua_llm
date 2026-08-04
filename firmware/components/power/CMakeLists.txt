# Select Lua implementation component based on LUA_SOURCE (top-level cache var)
if(LUA_SOURCE STREQUAL "registry")
    set(_lua_dep lua_alt)
else()
    set(_lua_dep lua)
endif()

idf_component_register(
    SRCS "power_mgmt.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES esp_pm ble ${_lua_dep}
)

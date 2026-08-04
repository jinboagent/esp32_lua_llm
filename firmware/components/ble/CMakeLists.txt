# Select Lua implementation component based on LUA_SOURCE (top-level cache var)
if(LUA_SOURCE STREQUAL "registry")
    set(_lua_dep lua_alt)
else()
    set(_lua_dep lua)
endif()

idf_component_register(
    SRCS "ble_nimble_init.c" "ble_scan.c" "scan_pipeline.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES bt esp_timer nvs_flash freertos proto json_enc filter usb ${_lua_dep}
)

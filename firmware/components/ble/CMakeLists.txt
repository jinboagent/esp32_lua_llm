# Select Lua implementation component based on LUA_SOURCE (top-level cache var)
if(LUA_SOURCE STREQUAL "registry")
    set(_lua_dep lua_alt)
else()
    set(_lua_dep lua)
endif()

# F2.4: the optional connection feature is a CMake-gated source file. The
# Kconfig symbol (BLE_CONN_ENABLED) is visible here as CONFIG_BLE_CONN_ENABLED;
# with the feature off, ble_conn.c is not even compiled.
set(_ble_srcs "ble_nimble_init.c" "ble_scan.c" "scan_pipeline.c")
if(CONFIG_BLE_CONN_ENABLED)
    list(APPEND _ble_srcs "ble_conn.c")
endif()

idf_component_register(
    SRCS ${_ble_srcs}
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES bt esp_timer nvs_flash freertos proto json_enc filter usb ${_lua_dep}
)

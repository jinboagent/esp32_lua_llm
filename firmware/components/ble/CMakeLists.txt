idf_component_register(
    SRCS "ble_nimble_init.c" "ble_scan.c" "scan_pipeline.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES bt esp_timer nvs_flash freertos proto json_enc filter usb lua
)

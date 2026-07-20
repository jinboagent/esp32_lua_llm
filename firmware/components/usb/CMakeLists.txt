idf_component_register(
    SRCS "usb_cdc_console.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES driver freertos
)

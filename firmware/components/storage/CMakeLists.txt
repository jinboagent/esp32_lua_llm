idf_component_register(
    SRCS "littlefs_storage.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES freertos littlefs
)

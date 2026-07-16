idf_component_register(
    SRCS "json_encoder.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES proto
)

idf_component_register(
    SRCS "filter_engine.c"
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES proto freertos
)

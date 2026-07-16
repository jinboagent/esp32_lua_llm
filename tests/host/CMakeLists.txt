cmake_minimum_required(VERSION 3.16)
project(ble_sniffer_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Include paths
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(IFACE_DIR ${PROJECT_ROOT}/interfaces)
set(UNITY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/unity/src)

# Source files
set(SRC_PROTO   ${PROJECT_ROOT}/firmware/components/proto/proto_adv_parse.c)
set(SRC_JSON    ${PROJECT_ROOT}/firmware/components/json_enc/json_encoder.c)
set(SRC_FILTER  ${PROJECT_ROOT}/firmware/components/filter/filter_engine.c)
set(SRC_UNITY   ${UNITY_DIR}/unity.c)

set(TEST_MAIN   ${CMAKE_CURRENT_SOURCE_DIR}/test_main.c)
set(TEST_PROTO  ${CMAKE_CURRENT_SOURCE_DIR}/test_adv_parser.c)
set(TEST_JSON   ${CMAKE_CURRENT_SOURCE_DIR}/test_json_encoder.c)
set(TEST_FILTER ${CMAKE_CURRENT_SOURCE_DIR}/test_filter_engine.c)

add_executable(test_runner
    ${TEST_MAIN}
    ${TEST_PROTO}
    ${TEST_JSON}
    ${TEST_FILTER}
    ${SRC_PROTO}
    ${SRC_JSON}
    ${SRC_FILTER}
    ${SRC_UNITY}
)

target_include_directories(test_runner PRIVATE
    ${IFACE_DIR}
    ${UNITY_DIR}
    ${PROJECT_ROOT}/firmware/components/proto
    ${PROJECT_ROOT}/firmware/components/json_enc
    ${PROJECT_ROOT}/firmware/components/filter
)

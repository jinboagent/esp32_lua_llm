cmake_minimum_required(VERSION 3.16)
project(ble_bridge_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Optional gcov instrumentation: configure with -DENABLE_COVERAGE=ON,
# run the tests, then read the .gcda/.gcno files (mingw gcov) to find
# untested error branches. See tests/README.md.
option(ENABLE_COVERAGE "Build host tests with gcov instrumentation" OFF)
if(ENABLE_COVERAGE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Include paths
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(IFACE_DIR ${PROJECT_ROOT}/interfaces)
set(UNITY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/unity/src)

# Source files
set(SRC_PROTO   ${PROJECT_ROOT}/firmware/components/proto/proto_adv_parse.c)
set(SRC_JSON    ${PROJECT_ROOT}/firmware/components/json_enc/json_encoder.c)
set(SRC_FILTER  ${PROJECT_ROOT}/firmware/components/filter/filter_engine.c)
set(SRC_CLI     ${PROJECT_ROOT}/firmware/components/cli/cli_commands.c)
set(SRC_BRIDGE  ${PROJECT_ROOT}/firmware/components/bridge/lua_llm_bridge.c)
set(SRC_POOL    ${PROJECT_ROOT}/firmware/components/lua/lua_pool.c)
set(SRC_PACK    ${PROJECT_ROOT}/firmware/components/pack_store/pack_store.c)
set(SRC_STUBS   ${CMAKE_CURRENT_SOURCE_DIR}/test_stubs.c)
set(SRC_UNITY   ${UNITY_DIR}/unity.c)

set(TEST_MAIN   ${CMAKE_CURRENT_SOURCE_DIR}/test_main.c)
set(TEST_PROTO  ${CMAKE_CURRENT_SOURCE_DIR}/test_adv_parser.c)
set(TEST_JSON   ${CMAKE_CURRENT_SOURCE_DIR}/test_json_encoder.c)
set(TEST_FILTER ${CMAKE_CURRENT_SOURCE_DIR}/test_filter_engine.c)
set(TEST_CLI    ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.c)
set(TEST_BRIDGE ${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.c)
set(TEST_POOL   ${CMAKE_CURRENT_SOURCE_DIR}/test_lua_pool.c)
set(TEST_CONN   ${CMAKE_CURRENT_SOURCE_DIR}/test_ble_conn.c)
set(TEST_RESP   ${CMAKE_CURRENT_SOURCE_DIR}/test_cli_responses.c)
set(TEST_FUZZ   ${CMAKE_CURRENT_SOURCE_DIR}/test_fuzz.c)
set(TEST_PACK   ${CMAKE_CURRENT_SOURCE_DIR}/test_pack.c)
set(TEST_CHUNK  ${CMAKE_CURRENT_SOURCE_DIR}/test_lua_chunk.c)

add_executable(test_runner
    ${TEST_MAIN}
    ${TEST_PROTO}
    ${TEST_JSON}
    ${TEST_FILTER}
    ${TEST_CLI}
    ${TEST_BRIDGE}
    ${TEST_POOL}
    ${TEST_CONN}
    ${TEST_RESP}
    ${TEST_FUZZ}
    ${TEST_PACK}
    ${TEST_CHUNK}
    ${SRC_PROTO}
    ${SRC_JSON}
    ${SRC_FILTER}
    ${SRC_CLI}
    ${SRC_BRIDGE}
    ${SRC_POOL}
    ${SRC_PACK}
    ${SRC_STUBS}
    ${SRC_UNITY}
)

target_include_directories(test_runner PRIVATE
    ${IFACE_DIR}
    ${UNITY_DIR}
    ${PROJECT_ROOT}/firmware/components/proto
    ${PROJECT_ROOT}/firmware/components/json_enc
    ${PROJECT_ROOT}/firmware/components/filter
    ${PROJECT_ROOT}/firmware/components/cli
    ${PROJECT_ROOT}/firmware/components/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# filter_engine.c compiles its locking out in host builds
target_compile_definitions(test_runner PRIVATE HOST_BUILD)

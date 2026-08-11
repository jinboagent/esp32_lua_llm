/*
 * Host tests for the CLI command interface (F4.1).
 *
 * cli_commands.c is compiled against controllable stubs of the
 * ble/lua/script/storage subsystems so the state machine and the
 * parser can be exercised without hardware.
 */

#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "cli_if.h"
#include "ble_if.h"
#include "test_stubs.h"

/* ---- Helpers ----------------------------------------------------------- */

#define RESP_LEN 1024
static char resp[RESP_LEN];

static void reset_stubs(void)
{
    stub_reset_all();
}

/* ---- Tests -------------------------------------------------------------- */

static void test_null_params(void)
{
    TEST_ASSERT_EQUAL_INT(CLI_ERR_NULL,
        cli_process_command(NULL, resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_NULL,
        cli_process_command("STATUS", NULL, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_NULL,
        cli_process_command("STATUS", resp, 0));
}

static void test_unknown_command(void)
{
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("BLAH", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"unknown command\"") != NULL);
}

static void test_version(void)
{
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("VERSION", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"version\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"firmware\":") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "esp32s3") != NULL);
}

static void test_scan_start_stop_state_machine(void)
{
    /* IDLE -> SCANNING */
    TEST_ASSERT_EQUAL(CLI_STATE_IDLE, cli_get_state());
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("SCAN START", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"scan_start\"") != NULL);
    TEST_ASSERT_EQUAL(CLI_STATE_SCANNING, cli_get_state());

    /* SCAN START while scanning -> "already scanning" */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("SCAN START", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "already scanning") != NULL);

    /* SCANNING -> IDLE */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("SCAN STOP", resp, RESP_LEN));
    TEST_ASSERT_EQUAL(CLI_STATE_IDLE, cli_get_state());

    /* SCAN STOP in IDLE -> "not scanning" */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("SCAN STOP", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "not scanning") != NULL);
}

static void test_scan_start_rollback(void)
{
    stub_pipeline_start_ret = -1;
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("SCAN START", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "pipeline failed") != NULL);
    /* Rollback must leave the device in IDLE */
    TEST_ASSERT_EQUAL(CLI_STATE_IDLE, cli_get_state());
    stub_pipeline_start_ret = 0;
}

static void test_scan_interval(void)
{
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCAN INTERVAL 500", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"value\":500") != NULL);
    TEST_ASSERT_EQUAL_UINT32(500, stub_interval_ms);

    /* Out of range */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCAN INTERVAL 0", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid value") != NULL);

    /* Rejected while scanning (-911) */
    cli_process_command("SCAN START", resp, RESP_LEN);
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("SCAN INTERVAL 500", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("SCAN INTERVAL 500", resp, RESP_LEN));
    cli_process_command("SCAN STOP", resp, RESP_LEN);
}

static void test_filter_add_list_clear(void)
{
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("FILTER ADD NAME Sensor*", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"index\":0") != NULL);

    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("FILTER ADD RSSI -70", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"index\":1") != NULL);

    TEST_ASSERT_EQUAL_INT(0, cli_process_command("FILTER LIST", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"count\":2") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"pattern\":\"Sensor*\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"threshold\":-70") != NULL);

    TEST_ASSERT_EQUAL_INT(0, cli_process_command("FILTER CLEAR", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("FILTER LIST", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"count\":0") != NULL);
}

static void test_filter_rssi_validation(void)
{
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("FILTER ADD RSSI 999", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "-128..127") != NULL);
}

static void test_filter_modify_while_scanning_rejected(void)
{
    cli_process_command("SCAN START", resp, RESP_LEN);
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("FILTER ADD NAME x", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("FILTER CLEAR", resp, RESP_LEN));
    /* LIST stays allowed while scanning */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("FILTER LIST", resp, RESP_LEN));
    cli_process_command("SCAN STOP", resp, RESP_LEN);
}

static void test_script_run_state_machine(void)
{
    /* SCRIPT RUN in IDLE -> -911 */
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("SCRIPT RUN", resp, RESP_LEN));

    /* Upload a script, start scanning, run */
    cli_process_command("SCRIPT BEGIN", resp, RESP_LEN);
    cli_process_command("SCRIPT CHUNK 72657475726E2031", resp, RESP_LEN);
    cli_process_command("SCRIPT END", resp, RESP_LEN);
    TEST_ASSERT_TRUE(stub_script_loaded);
    cli_process_command("SCAN START", resp, RESP_LEN);

    TEST_ASSERT_EQUAL_INT(0, cli_process_command("SCRIPT RUN", resp, RESP_LEN));
    TEST_ASSERT_EQUAL(CLI_STATE_SCRIPT_RUNNING, cli_get_state());

    /* SCAN START / SCAN STOP / FILTER ADD blocked while running */
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("SCAN START", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("SCAN STOP", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("FILTER ADD NAME x", resp, RESP_LEN));
    /* SCRIPT RUN again -> already running */
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("SCRIPT RUN", resp, RESP_LEN));

    /* SCRIPT STOP -> back to SCANNING */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("SCRIPT STOP", resp, RESP_LEN));
    TEST_ASSERT_EQUAL(CLI_STATE_SCANNING, cli_get_state());

    /* SCRIPT STOP with nothing running -> -911 */
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("SCRIPT STOP", resp, RESP_LEN));

    cli_process_command("SCAN STOP", resp, RESP_LEN);
}

static void test_status_fields(void)
{
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("STATUS", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"status\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"state\":\"idle\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"scanning\":false") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"free_storage\":12345") != NULL);
    /* H4 metrics present (stubbed pool values) */
    TEST_ASSERT_TRUE(strstr(resp, "\"free_heap\":") != NULL);
    TEST_ASSERT_TRUE(strstr(resp,
        "\"lua_pool\":{\"used\":1111,\"peak\":2222}") != NULL);

    cli_process_command("SCAN START", resp, RESP_LEN);
    cli_process_command("STATUS", resp, RESP_LEN);
    TEST_ASSERT_TRUE(strstr(resp, "\"state\":\"scanning\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"scanning\":true") != NULL);
    cli_process_command("SCAN STOP", resp, RESP_LEN);
}

static void test_response_buffer_overflow(void)
{
    char tiny[8];
    TEST_ASSERT_EQUAL_INT(CLI_ERR_BUFFER,
        cli_process_command("STATUS", tiny, sizeof(tiny)));
}

static void test_syntax_errors(void)
{
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("SCAN", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid syntax") != NULL);

    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("FILTER ADD", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid syntax") != NULL);

    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("FILTER ADD NAME", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid syntax") != NULL);

    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("SCRIPT BOGUS", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid syntax") != NULL);
}

static void test_script_chunk_hex_validation(void)
{
    /* Odd length and non-hex chars are rejected */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT CHUNK abc", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid hex") != NULL);
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT CHUNK zz", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid hex") != NULL);
}

static void test_power_commands(void)
{
    /* Default: sleep enabled, idle -> light_sleep state */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("POWER STATUS", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"sleep_enabled\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"state\":\"light_sleep\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"est_current_ma\":8") != NULL);

    /* Disable sleep -> active state, higher estimate */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("POWER SLEEP OFF", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"enabled\":false") != NULL);
    cli_process_command("POWER STATUS", resp, RESP_LEN);
    TEST_ASSERT_TRUE(strstr(resp, "\"state\":\"active\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"est_current_ma\":30") != NULL);

    /* Scanning overrides state and estimate */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("POWER SLEEP ON", resp, RESP_LEN));
    cli_process_command("SCAN START", resp, RESP_LEN);
    cli_process_command("POWER STATUS", resp, RESP_LEN);
    TEST_ASSERT_TRUE(strstr(resp, "\"state\":\"active\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"est_current_ma\":45") != NULL);
    cli_process_command("SCAN STOP", resp, RESP_LEN);

    /* Syntax errors */
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("POWER", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid syntax") != NULL);
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("POWER NAP", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "invalid syntax") != NULL);
}

/* ---- LUA EXEC response escaping (H1 regression) -------------------------- */

static void test_lua_exec_result_escaped(void)
{
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result),
             "a\"b\\c\nd");
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("LUA EXEC return x", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"status\":\"ok\"") != NULL);
    /* result must be embedded escaped: a\"b\\c\nd */
    TEST_ASSERT_NOT_NULL(strstr(resp, "\"result\":\"a\\\"b\\\\c\\nd\""));
}

static void test_lua_exec_error_escaped(void)
{
    stub_lua_exec_ret = -612;
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result),
             "[string \"return +++\"]:1: unexpected symbol near '+'");
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("LUA EXEC return +++", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-612") != NULL);
    /* quotes from the Lua message must be escaped (pre-H1 they were raw,
     * producing unparseable JSON) */
    TEST_ASSERT_NOT_NULL(strstr(resp, "[string \\\"return +++\\\"]"));
}

static void test_filter_value_echo_escaped(void)
{
    /* user-supplied filter text must round-trip escaped */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("FILTER ADD NAME ab\"cd", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"status\":\"ok\"") != NULL);
    TEST_ASSERT_NOT_NULL(strstr(resp, "ab\\\"cd"));
}

/* ---- Entry point --------------------------------------------------------- */

int test_cli_main(void)
{
    UNITY_BEGIN();

    cli_init();

    reset_stubs(); RUN_TEST(test_null_params);
    reset_stubs(); RUN_TEST(test_unknown_command);
    reset_stubs(); RUN_TEST(test_version);
    reset_stubs(); cli_init(); RUN_TEST(test_scan_start_stop_state_machine);
    reset_stubs(); cli_init(); RUN_TEST(test_scan_start_rollback);
    reset_stubs(); cli_init(); RUN_TEST(test_scan_interval);
    reset_stubs(); cli_init(); RUN_TEST(test_filter_add_list_clear);
    reset_stubs(); cli_init(); RUN_TEST(test_filter_rssi_validation);
    reset_stubs(); cli_init(); RUN_TEST(test_filter_modify_while_scanning_rejected);
    reset_stubs(); cli_init(); RUN_TEST(test_script_run_state_machine);
    reset_stubs(); cli_init(); RUN_TEST(test_status_fields);
    reset_stubs(); cli_init(); RUN_TEST(test_response_buffer_overflow);
    reset_stubs(); cli_init(); RUN_TEST(test_syntax_errors);
    reset_stubs(); cli_init(); RUN_TEST(test_script_chunk_hex_validation);
    reset_stubs(); cli_init(); RUN_TEST(test_power_commands);
    reset_stubs(); cli_init(); RUN_TEST(test_lua_exec_result_escaped);
    reset_stubs(); cli_init(); RUN_TEST(test_lua_exec_error_escaped);
    reset_stubs(); cli_init(); RUN_TEST(test_filter_value_echo_escaped);

    return UNITY_END();
}

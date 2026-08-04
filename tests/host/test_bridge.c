/*
 * Host tests for the LLM script bridge (F4.2).
 *
 * Exercises the text-line upload protocol against the shared subsystem
 * stubs: sandbox scan, size limits, empty-script rejection, upload
 * interruption by CLI commands, and the SCRIPT LOAD / SCRIPT END flow
 * through the CLI dispatcher.
 */

#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "bridge_if.h"
#include "cli_if.h"
#include "test_stubs.h"

#define RESP_LEN 1024
static char resp[RESP_LEN];

/* ---- Direct bridge API tests ---- */

static void test_bridge_null_params(void)
{
    TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_NULL,
        bridge_handle_script_upload(NULL, 4, resp, RESP_LEN));
    bridge_upload_begin(resp, RESP_LEN);
    TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_NULL,
        bridge_handle_script_upload("x", 1, NULL, RESP_LEN));
    bridge_abort();
}

static void test_bridge_data_without_load(void)
{
    TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_STATE,
        bridge_handle_script_upload("return 1", 8, resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "no upload in progress") != NULL);
}

static void test_bridge_load_and_silent_lines(void)
{
    TEST_ASSERT_EQUAL_INT(0, bridge_upload_begin(resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"script_load\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"msg\":\"ready\"") != NULL);
    TEST_ASSERT_TRUE(bridge_is_uploading());

    /* LOAD twice -> rejected */
    TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_STATE,
        bridge_upload_begin(resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "already uploading") != NULL);

    /* Data lines are silent */
    TEST_ASSERT_EQUAL_INT(0,
        bridge_handle_script_upload("local x = 1", 11, resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0, resp[0]);  /* empty response */

    /* END finalizes; size includes the appended newlines */
    TEST_ASSERT_EQUAL_INT(0, bridge_upload_finish(resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"script_end\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"size\":12") != NULL);
    TEST_ASSERT_FALSE(bridge_is_uploading());
    TEST_ASSERT_EQUAL(BRIDGE_STATE_VALIDATED, bridge_get_state());
}

static void test_bridge_sandbox_violations(void)
{
    struct { const char *line; } bad[] = {
        { "os.execute(\"ls\")" },
        { "local f = io.popen(\"dir\")" },
        { "dofile(\"/x.lua\")" },
        { "loadfile(\"y\")" },
        { "debug.getinfo(1)" },
        { "collectgarbage(\"count\")" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL_INT(0, bridge_upload_begin(resp, RESP_LEN));
        TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_REJECTED,
            bridge_handle_script_upload(bad[i].line,
                                        (uint32_t)strlen(bad[i].line),
                                        resp, RESP_LEN));
        TEST_ASSERT_TRUE(strstr(resp, "sandbox violation") != NULL);
        TEST_ASSERT_FALSE(bridge_is_uploading());  /* upload aborted */
    }

    /* Word-boundary check: identifiers merely containing a token pass */
    TEST_ASSERT_EQUAL_INT(0, bridge_upload_begin(resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0,
        bridge_handle_script_upload("local reloaded = true", 21,
                                    resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0, resp[0]);
    bridge_abort();
}

static void test_bridge_empty_script(void)
{
    TEST_ASSERT_EQUAL_INT(0, bridge_upload_begin(resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_REJECTED,
        bridge_upload_finish(resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "empty script") != NULL);
    TEST_ASSERT_FALSE(bridge_is_uploading());
}

static void test_bridge_oversize(void)
{
    static char big[9000];
    memset(big, 'a', sizeof(big));

    TEST_ASSERT_EQUAL_INT(0, bridge_upload_begin(resp, RESP_LEN));
    /* 9000 bytes > 8191 capacity -> -803, upload aborted */
    TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_TOO_BIG,
        bridge_handle_script_upload(big, sizeof(big), resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "exceeds") != NULL);
    TEST_ASSERT_FALSE(bridge_is_uploading());
}

static void test_bridge_compile_error(void)
{
    stub_upload_end_ret = -612;
    TEST_ASSERT_EQUAL_INT(0, bridge_upload_begin(resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0,
        bridge_handle_script_upload("function foo(", 13, resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(-612, bridge_upload_finish(resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-612") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "compile error") != NULL);
    stub_upload_end_ret = 0;
}

/* ---- End-to-end through the CLI dispatcher ---- */

static void test_cli_text_upload_flow(void)
{
    /* SCRIPT LOAD via CLI */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT LOAD", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"msg\":\"ready\"") != NULL);
    TEST_ASSERT_TRUE(bridge_is_uploading());

    /* Script text lines: silent, not echoed as commands */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("function on_adv(a, n, r)", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0, resp[0]);
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("  return true", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0, resp[0]);
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("end", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0, resp[0]);

    /* SCRIPT END finalizes */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT END", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"script_end\"") != NULL);
    TEST_ASSERT_FALSE(bridge_is_uploading());

    /* RUN works from SCANNING state with the uploaded script */
    cli_process_command("SCAN START", resp, RESP_LEN);
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT RUN", resp, RESP_LEN));
    TEST_ASSERT_EQUAL(CLI_STATE_SCRIPT_RUNNING, cli_get_state());
    cli_process_command("SCRIPT STOP", resp, RESP_LEN);
    cli_process_command("SCAN STOP", resp, RESP_LEN);
}

static void test_cli_upload_interrupted_by_command(void)
{
    /* TC-4: SCRIPT LOAD -> partial script -> SCAN START aborts the
     * upload and then proceeds with the command. */
    cli_process_command("SCRIPT LOAD", resp, RESP_LEN);
    cli_process_command("local half = true", resp, RESP_LEN);
    TEST_ASSERT_TRUE(bridge_is_uploading());

    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCAN START", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"scan_start\"") != NULL);
    TEST_ASSERT_FALSE(bridge_is_uploading());  /* aborted */

    /* END now reports no upload in progress (subsystem error rides in
     * the JSON; the CLI returns 0 because the command parsed fine) */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT END", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-611") != NULL);

    cli_process_command("SCAN STOP", resp, RESP_LEN);
}

static void test_hex_path_unaffected(void)
{
    /* The hex-chunk protocol still works alongside the text protocol */
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT BEGIN", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT CHUNK 72657475726E2031", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("SCRIPT END", resp, RESP_LEN));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"script_end\"") != NULL);
    TEST_ASSERT_TRUE(stub_script_loaded);
}

/* ---- Entry point ---- */

int test_bridge_main(void)
{
    UNITY_BEGIN();

    bridge_init(); cli_init();

    stub_reset_all(); RUN_TEST(test_bridge_null_params);
    stub_reset_all(); RUN_TEST(test_bridge_data_without_load);
    stub_reset_all(); RUN_TEST(test_bridge_load_and_silent_lines);
    stub_reset_all(); RUN_TEST(test_bridge_sandbox_violations);
    stub_reset_all(); RUN_TEST(test_bridge_empty_script);
    stub_reset_all(); RUN_TEST(test_bridge_oversize);
    stub_reset_all(); RUN_TEST(test_bridge_compile_error);
    stub_reset_all(); RUN_TEST(test_cli_text_upload_flow);
    stub_reset_all(); RUN_TEST(test_cli_upload_interrupted_by_command);
    stub_reset_all(); RUN_TEST(test_hex_path_unaffected);

    return UNITY_END();
}

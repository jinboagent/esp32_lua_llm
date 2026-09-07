/*
 * Host tests: the LUA BEGIN ... LUA END chunk session (H6.1 M3).
 *
 * A longer program uploads as text lines (fail-closed scanned, buffer
 * capped) and executes as ONE chunk on END — locals persist across
 * lines. Verified at protocol level through cli_process_command with
 * the lua exec stub capturing the assembled chunk.
 */
#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "cli_if.h"
#include "test_stubs.h"
#include "tv_json.h"

#define RESP_LEN 1024
static char resp[RESP_LEN];
static int  n_checked;

static int run(const char *cmd)
{
    int rc = cli_process_command(cmd, resp, RESP_LEN);
    if (resp[0] != '\0') {
        if (!tvj_valid(resp)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "'%s' response is NOT valid JSON: %.100s", cmd, resp);
            TEST_FAIL_MESSAGE(msg);
        }
        n_checked++;
    }
    return rc;
}

static void fresh(void)
{
    stub_reset_all();
    cli_init();
    /* no public reset for the chunk session; END or a CLI command
     * clears it — start each test clean by forcing END if active */
    run("LUA END");
}

static void test_chunk_assembles_and_executes_as_one_chunk(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("LUA BEGIN"));
    TEST_ASSERT_EQUAL_INT(0, run("local x = 21"));
    TEST_ASSERT_EQUAL_INT(0, run("return x * 2"));
    TEST_ASSERT_EQUAL_INT(0, run("LUA END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"lua_end\"") != NULL);
    /* the stub captured the assembled chunk: both lines, ONE exec,
     * joined with newlines and a trailing newline */
    TEST_ASSERT_EQUAL_STRING("local x = 21\nreturn x * 2\n",
                             stub_lua_exec_last);
}

static void test_result_echoed_from_exec(void)
{
    fresh();
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result), "42");
    TEST_ASSERT_EQUAL_INT(0, run("LUA BEGIN"));
    TEST_ASSERT_EQUAL_INT(0, run("local x = 21"));
    TEST_ASSERT_EQUAL_INT(0, run("return x * 2"));
    TEST_ASSERT_EQUAL_INT(0, run("LUA END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"result\":\"42\"") != NULL);
}

static void test_forbidden_line_aborts_mid_upload(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("LUA BEGIN"));
    TEST_ASSERT_EQUAL_INT(0, run("t = os.time()"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-612") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "sandbox violation") != NULL);
    /* session aborted: END now reports no-chunk-in-progress */
    TEST_ASSERT_EQUAL_INT(0, run("LUA END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-911") != NULL);
}

static void test_buffer_overflow_aborts(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("LUA BEGIN"));
    char big[260];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    for (int i = 0; i < 20; i++) {          /* 20 x ~260 B > 4096 */
        TEST_ASSERT_EQUAL_INT(0, run(big));
        if (strstr(resp, "-803") != NULL)
            break;
    }
    TEST_ASSERT_TRUE(strstr(resp, "-803") != NULL);
    TEST_ASSERT_EQUAL_INT(0, run("LUA END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-911") != NULL);
}

static void test_cli_command_aborts_chunk(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("LUA BEGIN"));
    TEST_ASSERT_EQUAL_INT(0, run("x = 1"));
    TEST_ASSERT_EQUAL_INT(0, run("STATUS"));       /* recognized command */
    TEST_ASSERT_EQUAL_INT(0, run("LUA END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-911") != NULL);
}

static void test_end_without_begin_is_state_error(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("LUA END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"lua_end\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-911") != NULL);
}

static void test_begin_mid_session_restarts(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("LUA BEGIN"));
    TEST_ASSERT_EQUAL_INT(0, run("x = 1"));
    /* a recognized CLI command aborts the active chunk first, so a
     * second BEGIN starts a FRESH session — abort-first semantics,
     * the same rule the F4.2 bridge and pack uploads follow */
    TEST_ASSERT_EQUAL_INT(0, run("LUA BEGIN"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"lua_begin\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"status\":\"ok\"") != NULL);
    TEST_ASSERT_EQUAL_INT(0, run("return 5"));
    TEST_ASSERT_EQUAL_INT(0, run("LUA END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"lua_end\"") != NULL);
    /* the executed chunk holds only the post-restart line */
    TEST_ASSERT_EQUAL_STRING("return 5\n", stub_lua_exec_last);
}

static void test_single_line_exec_still_works_alongside(void)
{
    fresh();
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result), "7");
    TEST_ASSERT_EQUAL_INT(0, run("LUA EXEC return 7"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"lua_exec\"") != NULL);
}

/* ---- Entry point --------------------------------------------------------- */

int test_lua_chunk_main(void)
{
    UNITY_BEGIN();
    n_checked = 0;

    fresh(); RUN_TEST(test_chunk_assembles_and_executes_as_one_chunk);
    fresh(); RUN_TEST(test_result_echoed_from_exec);
    fresh(); RUN_TEST(test_forbidden_line_aborts_mid_upload);
    fresh(); RUN_TEST(test_buffer_overflow_aborts);
    fresh(); RUN_TEST(test_cli_command_aborts_chunk);
    fresh(); RUN_TEST(test_end_without_begin_is_state_error);
    fresh(); RUN_TEST(test_begin_mid_session_restarts);
    fresh(); RUN_TEST(test_single_line_exec_still_works_alongside);

    printf("  (lua chunk contract: %d responses strictly validated)\n",
           n_checked);
    return UNITY_END();
}

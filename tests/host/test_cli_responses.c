/*
 * Host tests: the CLI response contract (2026-08-29).
 *
 * Every documented command family — ok path AND error path, both
 * argument forms where they exist — must produce a response that is
 * STRICT valid JSON and carries the expected "cmd" field. This is the
 * class-closer for the 2026-08-28 CONN TARGET bug: a conditional
 * format string emitted "chr":"…}" (unterminated string) for weeks
 * because no test ever validated the response bytes; the hw-suite
 * reader of the time silently skipped unparseable lines, so host tools
 * just timed out. Here the CLI runs against the existing stubs and
 * every response goes through a strict validator (tv_json.h).
 *
 * rc conventions (mirrors the existing suites): the CONN family always
 * returns 0 and reports errors in the JSON body; SCAN/FILTER/SCRIPT
 * state violations return CLI_ERR_STATE; NULL/buffer/unknown-command
 * return their CLI_ERR_* codes. Where a test's expectation of current
 * behavior differs from desired behavior, it fails loudly — that is
 * the point.
 */
#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "cli_if.h"
#include "test_stubs.h"
#include "tv_json.h"

#define RESP_LEN 1024
static char resp[RESP_LEN];

static int n_checked;

/* Run one command; assert rc; assert strict-JSON; assert cmd field. */
static void expect(const char *cmd, int rc_want, const char *cmd_field)
{
    int rc = cli_process_command(cmd, resp, RESP_LEN);
    if (rc != rc_want) {
        char msg[160];
        snprintf(msg, sizeof(msg), "'%s' rc %d, want %d, resp=%.80s",
                 cmd, rc, rc_want, resp);
        TEST_FAIL_MESSAGE(msg);
    }
    if (rc == CLI_ERR_NULL || resp[0] == '\0') {
        char msg[160];
        snprintf(msg, sizeof(msg), "'%s' produced no response body", cmd);
        TEST_FAIL_MESSAGE(msg);
    }
    if (!tvj_valid(resp)) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "'%s' response is NOT valid JSON: %.100s", cmd, resp);
        TEST_FAIL_MESSAGE(msg);
    }
    if (cmd_field != NULL && strstr(resp, cmd_field) == NULL) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "'%s' response missing %s: %.100s", cmd, cmd_field, resp);
        TEST_FAIL_MESSAGE(msg);
    }
    n_checked++;
}

static void fresh(void)
{
    stub_reset_all();
    cli_init();
}

/* ---- Scenarios ----------------------------------------------------------- */

static void test_boot_surface(void)
{
    expect("STATUS", 0, "\"cmd\":\"status\"");
    expect("VERSION", 0, "\"cmd\":\"version\"");
    expect("STATUS extra", CLI_ERR_INVALID_CMD, NULL);  /* unknown form */
    expect("BLAH", CLI_ERR_INVALID_CMD, NULL);
}

static void test_scan_family_all_valid(void)
{
    expect("SCAN START", 0, "\"cmd\":\"scan_start\"");
    expect("SCAN START", 0, "already scanning");
    expect("SCAN INTERVAL 500", CLI_ERR_STATE, NULL);
    expect("SCAN STOP", 0, "\"cmd\":\"scan_stop\"");
    expect("SCAN STOP", 0, "not scanning");
    expect("SCAN INTERVAL 500", 0, "\"value\":500");
    expect("SCAN INTERVAL 0", 0, "invalid value");
    expect("SCAN INTERVAL", CLI_ERR_INVALID_CMD, NULL);
    expect("SCAN", CLI_ERR_INVALID_CMD, NULL);
}

static void test_filter_family_all_valid(void)
{
    expect("FILTER ADD NAME Sensor*", 0, "\"index\":0");
    expect("FILTER ADD NAME ab\"cd", 0, "ab\\\"cd");
    expect("FILTER ADD RSSI -70", 0, "\"index\":2");
    expect("FILTER ADD RSSI 999", 0, "-128..127");
    expect("FILTER LIST", 0, "\"count\":3");
    expect("FILTER CLEAR", 0, NULL);
    expect("FILTER LIST", 0, "\"count\":0");
    expect("FILTER CLEAR", 0, NULL);           /* idempotent */
    expect("FILTER ADD", CLI_ERR_INVALID_CMD, NULL);
    expect("FILTER ADD NAME", CLI_ERR_INVALID_CMD, NULL);
}

static void test_script_family_all_valid(void)
{
    expect("SCRIPT RUN", CLI_ERR_STATE, NULL);   /* nothing loaded */
    expect("SCRIPT STOP", CLI_ERR_STATE, NULL);
    expect("SCRIPT BEGIN", 0, NULL);
    expect("SCRIPT CHUNK abc", 0, "invalid hex");
    expect("SCRIPT CHUNK zz", 0, "invalid hex");
    expect("SCRIPT CHUNK 72657475726E2031", 0, NULL); /* "return 1" */
    expect("SCRIPT END", 0, NULL);
    expect("SCRIPT BOGUS", CLI_ERR_INVALID_CMD, NULL);
    expect("SCRIPT", CLI_ERR_INVALID_CMD, NULL);
}

static void test_lua_exec_all_valid(void)
{
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result),
             "a\"b\\c\nd");
    expect("LUA EXEC return x", 0, "\"result\":\"a\\\"b\\\\c\\nd\"");
    stub_lua_exec_ret = -612;
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result),
             "[string \"return +++\"]:1: boom");
    expect("LUA EXEC return +++", 0, "\"code\":-612");
}

static void test_power_family_all_valid(void)
{
    expect("POWER STATUS", 0, "\"cmd\":\"power_status\"");
    expect("POWER SLEEP OFF", 0, NULL);
    expect("POWER SLEEP ON", 0, NULL);
    expect("POWER", CLI_ERR_INVALID_CMD, NULL);
    expect("POWER NAP", CLI_ERR_INVALID_CMD, NULL);
}

static void test_conn_family_all_valid(void)
{
    expect("CONN", CLI_ERR_INVALID_CMD, NULL);
    expect("CONN STATUS", 0, "\"cmd\":\"conn_status\"");
    expect("CONN TARGET 180F", 0, "\"svc\":\"180F\"");
    expect("CONN TARGET 180F 2A6E", 0, "\"chr\":\"2A6E\"");
    expect("CONN TARGET", CLI_ERR_INVALID_CMD, NULL);
    stub_conn_set_target_ret = -450;      /* stubs accept by default */
    expect("CONN TARGET zzzz", 0, "\"code\":-450");
    stub_conn_set_target_ret = 0;
    expect("CONN START", 0, "\"cmd\":\"conn_start\"");
    stub_conn_start_ret = -450;           /* stubs accept by default */
    expect("CONN START 00:11:22:33:44", 0, "\"code\":-450");
    stub_conn_start_ret = 0;
    expect("CONN INTERVAL 500", 0, "\"value\":500");
    expect("CONN INTERVAL 50", 0, "invalid value");
    expect("CONN STOP", 0, "\"cmd\":\"conn_stop\"");
    expect("CONN BOGUS", CLI_ERR_INVALID_CMD, NULL);
}

static void test_conn_error_paths_via_stubs(void)
{
    stub_conn_set_target_ret = -450;
    expect("CONN TARGET xyz", 0, "\"code\":-450");
    stub_reset_all();
    stub_conn_start_ret = -456;
    expect("CONN START", 0, "\"code\":-456");
    stub_reset_all();
    stub_conn_stop_ret = -453;
    expect("CONN STOP", 0, "\"code\":-453");
}

static void test_script_running_blocks_conn_all_valid(void)
{
    /* state matrix (review A1): TARGET/START/INTERVAL rejected with
     * -911 while a script runs; STOP/STATUS stay allowed */
    expect("SCRIPT BEGIN", 0, NULL);
    expect("SCRIPT CHUNK 72657475726E2031", 0, NULL);
    expect("SCRIPT END", 0, NULL);
    expect("SCAN START", 0, NULL);
    expect("SCRIPT RUN", 0, NULL);
    expect("CONN TARGET 180F 2A6E", CLI_ERR_STATE, "\"code\":-911");
    expect("CONN START", CLI_ERR_STATE, "\"code\":-911");
    expect("CONN INTERVAL 500", CLI_ERR_STATE, "\"code\":-911");
    expect("CONN STATUS", 0, "\"cmd\":\"conn_status\"");
    expect("CONN STOP", 0, "\"cmd\":\"conn_stop\"");
    expect("SCRIPT STOP", 0, NULL);
    expect("SCAN STOP", 0, NULL);
}

static void test_status_in_every_state(void)
{
    expect("STATUS", 0, "\"state\":\"idle\"");
    expect("SCAN START", 0, NULL);
    expect("STATUS", 0, "\"state\":\"scanning\"");
    expect("SCRIPT BEGIN", 0, NULL);
    expect("SCRIPT CHUNK 72657475726E2031", 0, NULL);
    expect("SCRIPT END", 0, NULL);
    expect("SCRIPT RUN", 0, NULL);
    expect("STATUS", 0, "\"script_running\":true");
    expect("SCRIPT STOP", 0, NULL);
    expect("SCAN STOP", 0, NULL);
}

static void test_pack_family_all_valid(void)
{
    expect("PACK", CLI_ERR_INVALID_CMD, NULL);
    expect("PACK BOGUS", CLI_ERR_INVALID_CMD, NULL);
    expect("PACK BEGIN demo", 0, "\"cmd\":\"pack_begin\"");
    /* a data line acks silently, exactly like the F4.2 bridge */
    char r2[256];
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("function manifest() return M end",
                            r2, sizeof(r2)));
    TEST_ASSERT_EQUAL_STRING("", r2);
    expect("PACK END", 0, "\"cmd\":\"pack_end\"");
    expect("PACK LIST", 0, "\"cmd\":\"pack_list\"");
    expect("PACK RUN demo", 0, "\"cmd\":\"pack_run\"");
    expect("PACK RUN ghost", 0, "\"code\":-621");
    expect("PACK AUTORUN demo ON", 0, "\"cmd\":\"pack_autorun\"");
    expect("PACK AUTORUN demo MAYBE", CLI_ERR_INVALID_CMD, NULL);
    expect("PACK DEL demo", 0, "\"cmd\":\"pack_del\"");
    expect("PACK DEL demo", 0, "\"code\":-706");
}

static void test_pack_end_compile_error_is_valid_json(void)
{
    /* audit B1: a pack compile error carries double quotes ('[string
     * "pack"]:1: ...'); the PACK END error response must still be strict
     * JSON (json_escape_str - the eval-2026-08-11 H1 class, reintroduced
     * on the pack path). */
    char r2[256];
    stub_lua_compile_ret = -612;
    expect("PACK BEGIN demo", 0, "\"cmd\":\"pack_begin\"");
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("return +++", r2, sizeof(r2)));
    expect("PACK END", 0, "\"code\":-612");
}

static void test_pack_begin_store_full_is_valid_json(void)
{
    /* audit B3: BEGIN rejects a 9th pack (-624) so nothing can live past
     * the fixed dirent budget; the error response joins the contract. */
    char r2[256];
    for (int i = 1; i <= 8; i++) {
        char begin[24], body[16];
        snprintf(begin, sizeof(begin), "PACK BEGIN s%d", i);
        snprintf(body, sizeof(body), "w%d = %d", i, i);
        expect(begin, 0, "\"cmd\":\"pack_begin\"");
        TEST_ASSERT_EQUAL_INT(0,
            cli_process_command(body, r2, sizeof(r2)));
        expect("PACK END", 0, "\"cmd\":\"pack_end\"");
    }
    expect("PACK BEGIN s9", 0, "\"code\":-624");
}

/* ---- Entry point --------------------------------------------------------- */

int test_cli_responses_main(void)
{
    UNITY_BEGIN();
    n_checked = 0;

    fresh(); RUN_TEST(test_boot_surface);
    fresh(); RUN_TEST(test_scan_family_all_valid);
    fresh(); RUN_TEST(test_filter_family_all_valid);
    fresh(); RUN_TEST(test_script_family_all_valid);
    fresh(); RUN_TEST(test_lua_exec_all_valid);
    fresh(); RUN_TEST(test_power_family_all_valid);
    fresh(); RUN_TEST(test_conn_family_all_valid);
    fresh(); RUN_TEST(test_conn_error_paths_via_stubs);
    fresh(); RUN_TEST(test_script_running_blocks_conn_all_valid);
    fresh(); RUN_TEST(test_status_in_every_state);
    fresh(); RUN_TEST(test_pack_family_all_valid);
    fresh(); RUN_TEST(test_pack_end_compile_error_is_valid_json);
    fresh(); RUN_TEST(test_pack_begin_store_full_is_valid_json);

    printf("  (response contract: %d responses strictly validated)\n",
           n_checked);
    return UNITY_END();
}

/*
 * Host tests: the PACK command family + pack_store (H6.1 M2 — named
 * tool packs in LittleFS, boot autorun).
 *
 * Protocol-level testing through cli_process_command against the fake
 * LittleFS (test_stubs): every response must be strict JSON (tv_json),
 * the fail-closed sandbox scan must abort mid-upload exactly like the
 * F4.2 bridge, markers must drive list/autorun/boot behavior.
 */
#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "cli_if.h"
#include "pack_if.h"
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
    pack_store_abort();
}

/* Upload a small pack through the real CLI protocol. */
static void upload(const char *name, bool autorun, const char *body)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PACK BEGIN %s%s", name,
             autorun ? " autorun" : "");
    TEST_ASSERT_EQUAL_INT(0, run(cmd));
    TEST_ASSERT_EQUAL_INT(0, run(body));
    TEST_ASSERT_EQUAL_INT(0, run("PACK END"));
}

static void test_pack_name_validation(void)
{
    fresh();
    TEST_ASSERT_TRUE(pack_store_name_ok("demo"));
    TEST_ASSERT_TRUE(pack_store_name_ok("pack_2"));
    TEST_ASSERT_FALSE(pack_store_name_ok(""));
    TEST_ASSERT_FALSE(pack_store_name_ok("bad/name"));
    TEST_ASSERT_FALSE(pack_store_name_ok("bad name"));
    TEST_ASSERT_FALSE(pack_store_name_ok("bad.name"));
    TEST_ASSERT_FALSE(pack_store_name_ok("this_name_is_way_too_long_x"));
    TEST_ASSERT_FALSE(pack_store_name_ok(NULL));
    /* via the CLI: -620 with a message */
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN bad/name"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-620") != NULL);
}

static void test_upload_persist_and_list(void)
{
    fresh();
    upload("demo", false, "DEMO_M = [[{}]]");
    uint32_t len = 0;
    const uint8_t *data = stub_fs_get("/littlefs/packs/demo.lua", &len);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT32(strlen("DEMO_M = [[{}]]") + 1, len); /* +\n */
    TEST_ASSERT_EQUAL_UINT8('D', data[0]);
    TEST_ASSERT_EQUAL_UINT8('\n', data[len - 1]);

    TEST_ASSERT_EQUAL_INT(0, run("PACK LIST"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"pack_list\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"name\":\"demo\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"autorun\":false") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"free\":") != NULL);
    TEST_ASSERT_NULL(stub_fs_get("/littlefs/packs/demo.autorun", NULL));
}

static void test_autorun_marker_lifecycle(void)
{
    fresh();
    upload("demo", true, "DEMO_M = [[{}]]");
    TEST_ASSERT_NOT_NULL(stub_fs_get("/littlefs/packs/demo.autorun", NULL));
    TEST_ASSERT_EQUAL_INT(0, run("PACK LIST"));
    TEST_ASSERT_TRUE(strstr(resp, "\"autorun\":true") != NULL);

    /* overwrite WITHOUT autorun clears the stale marker (M2 rule) */
    upload("demo", false, "DEMO_M = [[{}]]");
    TEST_ASSERT_NULL(stub_fs_get("/littlefs/packs/demo.autorun", NULL));

    TEST_ASSERT_EQUAL_INT(0, run("PACK AUTORUN demo ON"));
    TEST_ASSERT_TRUE(strstr(resp, "\"autorun\":true") != NULL);
    TEST_ASSERT_NOT_NULL(stub_fs_get("/littlefs/packs/demo.autorun", NULL));
    TEST_ASSERT_EQUAL_INT(0, run("PACK AUTORUN demo OFF"));
    TEST_ASSERT_NULL(stub_fs_get("/littlefs/packs/demo.autorun", NULL));
    /* ON for a missing pack -> -621 */
    TEST_ASSERT_EQUAL_INT(0, run("PACK AUTORUN ghost ON"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-621") != NULL);
}

static void test_forbidden_line_aborts_like_the_bridge(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN demo"));
    TEST_ASSERT_EQUAL_INT(0, run("t = os.time()"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-612") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "sandbox violation") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "'os.'") != NULL);
    /* session aborted: END now reports no-upload-in-progress */
    TEST_ASSERT_EQUAL_INT(0, run("PACK END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-611") != NULL);
    /* nothing was stored */
    TEST_ASSERT_NULL(stub_fs_get("/littlefs/packs/demo.lua", NULL));
}

static void test_compile_error_surfaces_in_end(void)
{
    fresh();
    stub_lua_compile_ret = -612;
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN demo"));
    TEST_ASSERT_EQUAL_INT(0, run("return +++"));
    TEST_ASSERT_EQUAL_INT(0, run("PACK END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-612") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "unexpected symbol") != NULL);
    TEST_ASSERT_NULL(stub_fs_get("/littlefs/packs/demo.lua", NULL));
}

static void test_empty_pack_rejected(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN demo"));
    TEST_ASSERT_EQUAL_INT(0, run("PACK END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-612") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "empty pack") != NULL);
}

static void test_pack_run_executes_stored_text(void)
{
    fresh();
    upload("demo", false, "function manifest() return M end");
    snprintf(stub_lua_exec_result, sizeof(stub_lua_exec_result), "ran");
    TEST_ASSERT_EQUAL_INT(0, run("PACK RUN demo"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"pack_run\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"result\":\"ran\"") != NULL);
    TEST_ASSERT_TRUE(strstr(stub_lua_exec_last,
                            "function manifest()") != NULL);
    /* run of a missing pack -> -621 */
    TEST_ASSERT_EQUAL_INT(0, run("PACK RUN ghost"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-621") != NULL);
}

static void test_pack_del_removes_file_and_marker(void)
{
    fresh();
    upload("demo", true, "DEMO_M = [[{}]]");
    TEST_ASSERT_EQUAL_INT(0, run("PACK DEL demo"));
    TEST_ASSERT_NULL(stub_fs_get("/littlefs/packs/demo.lua", NULL));
    TEST_ASSERT_NULL(stub_fs_get("/littlefs/packs/demo.autorun", NULL));
    TEST_ASSERT_EQUAL_INT(0, run("PACK RUN demo"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-621") != NULL);
}

static void test_cli_command_aborts_upload(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN demo"));
    TEST_ASSERT_EQUAL_INT(0, run("STATUS"));     /* recognized command */
    TEST_ASSERT_EQUAL_INT(0, run("PACK END"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-611") != NULL);
}

static void test_boot_autorun_runs_only_marked_packs(void)
{
    fresh();
    upload("aaa", true, "aaa_ran = 1");
    upload("zzz", false, "zzz_ran = 1");
    int ran = pack_store_boot_autorun();
    TEST_ASSERT_EQUAL_INT(1, ran);
    TEST_ASSERT_TRUE(strstr(stub_lua_exec_last, "aaa_ran") != NULL);
    TEST_ASSERT_FALSE(strstr(stub_lua_exec_last, "zzz_ran") != NULL);
}

static void test_two_packs_listed_and_state_machine_untouched(void)
{
    fresh();
    upload("one", false, "a1 = 1");
    upload("two", true, "a2 = 2");
    TEST_ASSERT_EQUAL_INT(0, run("PACK LIST"));
    TEST_ASSERT_TRUE(strstr(resp, "\"name\":\"one\"") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "\"name\":\"two\"") != NULL);
    /* packs live outside the CLI state machine (decision 11): LIST and
     * RUN stay valid even while "scanning" */
    TEST_ASSERT_EQUAL_INT(0, run("SCAN START"));
    TEST_ASSERT_EQUAL_INT(0, run("PACK RUN one"));
    TEST_ASSERT_TRUE(strstr(resp, "\"status\":\"ok\"") != NULL);
    TEST_ASSERT_EQUAL_INT(0, run("SCAN STOP"));
}

static void test_syntax_errors_covered(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD, run("PACK"));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD, run("PACK BOGUS"));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD, run("PACK BEGIN"));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD, run("PACK AUTORUN demo"));
}

/* ---- Entry point --------------------------------------------------------- */

int test_pack_main(void)
{
    UNITY_BEGIN();
    n_checked = 0;

    fresh(); RUN_TEST(test_pack_name_validation);
    fresh(); RUN_TEST(test_upload_persist_and_list);
    fresh(); RUN_TEST(test_autorun_marker_lifecycle);
    fresh(); RUN_TEST(test_forbidden_line_aborts_like_the_bridge);
    fresh(); RUN_TEST(test_compile_error_surfaces_in_end);
    fresh(); RUN_TEST(test_empty_pack_rejected);
    fresh(); RUN_TEST(test_pack_run_executes_stored_text);
    fresh(); RUN_TEST(test_pack_del_removes_file_and_marker);
    fresh(); RUN_TEST(test_cli_command_aborts_upload);
    fresh(); RUN_TEST(test_boot_autorun_runs_only_marked_packs);
    fresh(); RUN_TEST(test_two_packs_listed_and_state_machine_untouched);
    fresh(); RUN_TEST(test_syntax_errors_covered);

    printf("  (pack contract: %d responses strictly validated)\n",
           n_checked);
    return UNITY_END();
}

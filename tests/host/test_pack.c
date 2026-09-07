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

/* audit B3: each autorun pack occupies TWO dirents (.lua + .autorun);
 * a listing capped at PACK_MAX_FILES dirents silently drops packs once
 * markers exist. 6 packs / 3 markers = 9 dirents > 8. */
static void test_list_shows_all_packs_when_markers_eat_dirents(void)
{
    fresh();
    upload("p1", true,  "m1 = 1");
    upload("p2", true,  "m2 = 2");
    upload("p3", true,  "m3 = 3");
    upload("p4", false, "m4 = 4");
    upload("p5", false, "m5 = 5");
    upload("p6", false, "m6 = 6");
    TEST_ASSERT_EQUAL_INT(0, run("PACK LIST"));
    for (int i = 1; i <= 6; i++) {
        char want[16];
        snprintf(want, sizeof(want), "\"name\":\"p%d\"", i);
        TEST_ASSERT_TRUE_MESSAGE(strstr(resp, want) != NULL, want);
    }
}

/* audit B3, boot side: INTERLEAVED autorun uploads put 2 dirents per pack
 * at the front — 5 autorun packs = 10 dirents, so p5.lua lands past the
 * 8-dirent cap and the pack never boots. (Markers appended last would
 * still be found: existence is checked per-path, not via dirents.) */
static void test_boot_autorun_runs_marked_pack_past_dirent_cap(void)
{
    fresh();
    upload("p1", true, "m1 = 1");
    upload("p2", true, "m2 = 2");
    upload("p3", true, "m3 = 3");
    upload("p4", true, "m4 = 4");
    upload("p5", true, "m5 = 5");
    int ran = pack_store_boot_autorun();
    TEST_ASSERT_EQUAL_INT(5, ran);
    TEST_ASSERT_TRUE(strstr(stub_lua_exec_last, "m5 = 5") != NULL);
}

/* audit B3, door enforcement: a 9th pack could never be listed or
 * autorun (fixed dirent budget) — BEGIN must reject it, while
 * overwriting an existing name stays allowed. */
static void test_upload_rejected_when_store_full(void)
{
    fresh();
    for (int i = 1; i <= 8; i++) {
        char name[8], body[12];
        snprintf(name, sizeof(name), "q%d", i);
        snprintf(body, sizeof(body), "v%d = %d", i, i);
        upload(name, false, body);
    }
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN q9"));
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-624") != NULL);
    TEST_ASSERT_TRUE(strstr(resp, "store full") != NULL);
    /* overwrite of an existing name is exempt from the cap */
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN q1"));
    TEST_ASSERT_TRUE(strstr(resp, "\"cmd\":\"pack_begin\"") != NULL);
    TEST_ASSERT_EQUAL_INT(0, run("PACK END"));   /* empty: -612, q1 intact */
    TEST_ASSERT_TRUE(strstr(resp, "\"code\":-612") != NULL);
    TEST_ASSERT_NOT_NULL(stub_fs_get("/littlefs/packs/q1.lua", NULL));
    /* after DEL one, a new name fits again */
    TEST_ASSERT_EQUAL_INT(0, run("PACK DEL q8"));
    TEST_ASSERT_EQUAL_INT(0, run("PACK BEGIN q9"));
    TEST_ASSERT_TRUE(strstr(resp, "\"status\":\"ok\"") != NULL);
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
    fresh(); RUN_TEST(test_list_shows_all_packs_when_markers_eat_dirents);
    fresh(); RUN_TEST(test_boot_autorun_runs_marked_pack_past_dirent_cap);
    fresh(); RUN_TEST(test_upload_rejected_when_store_full);
    fresh(); RUN_TEST(test_syntax_errors_covered);

    printf("  (pack contract: %d responses strictly validated)\n",
           n_checked);
    return UNITY_END();
}

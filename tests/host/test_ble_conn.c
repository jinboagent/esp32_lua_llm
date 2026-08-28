/*
 * Host tests for the F2.4 BLE connection feature.
 *
 * Part 1 exercises the REAL json_encode_conn (merge / envelope-wins /
 * wrap / escaping / truncation) from json_encoder.c — pure C, no stubs.
 * Part 2 exercises the CONN command family in cli_commands.c against the
 * controllable ble_conn stubs (test_stubs.c), including the A1 state
 * matrix and the STATUS "conn" object.
 */

#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "json_if.h"
#include "cli_if.h"
#include "ble_if.h"
#include "test_stubs.h"

/* ---- Helpers ----------------------------------------------------------- */

#define RESP_LEN 1024
static char resp[RESP_LEN];
static char line[JSON_LINE_MAX_LEN];

/* Minimal validity check: balanced quotes/braces and parseable key set.
 * The full-tree parse is done by hand below (no JSON lib on host). */
static bool contains(const char *s, const char *sub)
{
    return strstr(s, sub) != NULL;
}

/* ---- Part 1: json_encode_conn ------------------------------------------ */

static void test_conn_merge_object(void)
{
    const char *payload = "{\"temp\":25.5,\"hum\":40}";
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 1234,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    /* exact output — substring checks missed the missing separators
     * between merged members (invalid JSON, fixed 2026-08-28) */
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1234,\"addr\":\"AA:BB:CC:DD:EE:FF\",\"src\":\"conn\","
        "\"temp\":25.5,\"hum\":40}",
        line);
    TEST_ASSERT_TRUE(contains(line, "\"src\":\"conn\""));
    TEST_ASSERT_TRUE(contains(line, "\"temp\":25.5"));
    TEST_ASSERT_TRUE(contains(line, "\"hum\":40"));
    /* envelope intact and first */
    TEST_ASSERT_TRUE(contains(line, "{\"ts\":1234,\"addr\":\"AA:BB:CC:DD:EE:FF\","
                                    "\"src\":\"conn\""));
    /* nothing wrapped */
    TEST_ASSERT_FALSE(contains(line, "\"data\""));
}

static void test_conn_envelope_keys_win(void)
{
    const char *payload = "{\"ts\":1,\"addr\":\"XX:XX:XX:XX:XX:XX\","
                          "\"src\":\"fake\",\"keep\":7}";
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 99,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    /* exact output — colliding members skipped, separators still valid */
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":99,\"addr\":\"AA:BB:CC:DD:EE:FF\",\"src\":\"conn\",\"keep\":7}",
        line);
    TEST_ASSERT_TRUE(contains(line, "\"ts\":99"));
    TEST_ASSERT_TRUE(contains(line, "\"addr\":\"AA:BB:CC:DD:EE:FF\""));
    TEST_ASSERT_FALSE(contains(line, "fake"));
    TEST_ASSERT_TRUE(contains(line, "\"keep\":7"));
    /* exactly one of each envelope key */
    TEST_ASSERT_EQUAL_INT(1, (int)(strstr(line, "\"ts\"") != NULL));
}

static void test_conn_wrap_non_object(void)
{
    const char *payload = "hello 25.5";
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 5,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(contains(line, "\"data\":\"hello 25.5\""));
}

static void test_conn_wrap_escapes(void)
{
    const char *payload = "a\"b\\c\nd";
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 5,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(contains(line, "\\\"b\\\\c\\nd"));
}

static void test_conn_merge_nested_values(void)
{
    const char *payload = "{\"o\":{\"x\":1},\"arr\":[1,2],\"b\":true,\"n\":null}";
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 1,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    /* exact output — nested values keep their separators too */
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1,\"addr\":\"AA:BB:CC:DD:EE:FF\",\"src\":\"conn\","
        "\"o\":{\"x\":1},\"arr\":[1,2],\"b\":true,\"n\":null}",
        line);
    TEST_ASSERT_TRUE(contains(line, "\"o\":{\"x\":1}"));
    TEST_ASSERT_TRUE(contains(line, "\"arr\":[1,2]"));
    TEST_ASSERT_TRUE(contains(line, "\"b\":true"));
    TEST_ASSERT_TRUE(contains(line, "\"n\":null"));
}

static void test_conn_braces_inside_strings(void)
{
    const char *payload = "{\"s\":\"a},b\",\"t\":2}";
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 1,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    /* merged, not wrapped — the '}' inside the string must not end the object */
    TEST_ASSERT_TRUE(contains(line, "\"s\":\"a},b\""));
    TEST_ASSERT_FALSE(contains(line, "\"data\""));
}

static void test_conn_malformed_object_wraps(void)
{
    const char *payload = "{\"temp\":}";   /* balanced but invalid member */
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 1,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(contains(line, "\"data\":\"{\\\"temp\\\":}\""));
}

static void test_conn_truncation_marks_and_stays_valid(void)
{
    /* Payload that cannot fit a 512-byte line after the envelope. */
    char payload[490];
    memset(payload, 'a', sizeof(payload));
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 1,
                              (const uint8_t *)payload, (uint16_t)sizeof(payload),
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(contains(line, "\"trunc\":true"));
    TEST_ASSERT_TRUE(line[0] == '{');
    TEST_ASSERT_TRUE(line[strlen(line) - 1] == '}');   /* closed line */

    /* Data must be shorter than the payload — something was cut. */
    const char *d = strstr(line, "\"data\":\"");
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_TRUE(strlen(d + 8) < sizeof(payload));
}

static void test_conn_empty_payload(void)
{
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 1, NULL, 0,
                              line, sizeof(line), NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(contains(line, "\"data\":\"\""));
}

static void test_conn_null_params(void)
{
    TEST_ASSERT_EQUAL_INT(-202,
        json_encode_conn(NULL, 1, (const uint8_t *)"x", 1, line, sizeof(line), NULL));
    TEST_ASSERT_EQUAL_INT(-202,
        json_encode_conn("AA", 1, (const uint8_t *)"x", 1, NULL, 100, NULL));
    TEST_ASSERT_EQUAL_INT(-202,
        json_encode_conn("AA", 1, NULL, 3, line, sizeof(line), NULL));
    TEST_ASSERT_EQUAL_INT(-203,
        json_encode_conn("AA", 1, (const uint8_t *)"x", 1, line, 1, NULL));
}

static void test_conn_out_len_matches(void)
{
    const char *payload = "{\"k\":1}";
    uint16_t out_len = 0;
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 7,
                              (const uint8_t *)payload, (uint16_t)strlen(payload),
                              line, sizeof(line), &out_len);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT((int)strlen(line), (int)out_len);
}

/* ---- Part 2: CONN command family (against stubs) ----------------------- */

static void test_conn_bare_is_syntax_error(void)
{
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("CONN", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "invalid syntax"));
}

static void test_conn_status_off(void)
{
    stub_reset_all();
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("CONN STATUS", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"cmd\":\"conn_status\""));
    TEST_ASSERT_TRUE(contains(resp, "\"state\":\"off\""));
    TEST_ASSERT_TRUE(contains(resp, "\"mode\":\"none\""));
    TEST_ASSERT_TRUE(contains(resp, "\"mtu\":0"));
}

static void test_conn_target_captures_uuids(void)
{
    stub_reset_all();
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("CONN TARGET 180F 2A6E", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"cmd\":\"conn_target\""));
    TEST_ASSERT_EQUAL_STRING("180F", stub_conn_target_svc);
    TEST_ASSERT_TRUE(stub_conn_target_chr_set);
    TEST_ASSERT_EQUAL_STRING("2A6E", stub_conn_target_chr);
}

static void test_conn_target_rejects_bad_uuid(void)
{
    stub_reset_all();
    stub_conn_set_target_ret = -450;
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("CONN TARGET xyz", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"status\":\"error\""));
    TEST_ASSERT_TRUE(contains(resp, "-450"));
}

static void test_conn_start_direct_captures_args(void)
{
    stub_reset_all();
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("CONN START AA:BB:CC:DD:EE:FF random",
                            resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"mode\":\"direct\""));
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", stub_conn_start_addr);
    TEST_ASSERT_EQUAL_STRING("random", stub_conn_start_type);
}

static void test_conn_start_relays_error_code(void)
{
    stub_reset_all();
    stub_conn_start_ret = -456;
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("CONN START", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"status\":\"error\""));
    TEST_ASSERT_TRUE(contains(resp, "-456"));
}

static void test_conn_stop_relays_not_connected(void)
{
    stub_reset_all();
    stub_conn_stop_ret = -453;
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("CONN STOP", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"status\":\"error\""));
    TEST_ASSERT_TRUE(contains(resp, "-453"));
}

static void test_conn_interval_validation(void)
{
    stub_reset_all();
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("CONN INTERVAL 50", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"status\":\"error\""));
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("CONN INTERVAL 500", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"value\":500"));
    TEST_ASSERT_EQUAL_UINT32(500, stub_conn_poll_ms);
    TEST_ASSERT_EQUAL_INT(0,
        cli_process_command("CONN INTERVAL 20000", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"status\":\"error\""));
}

static void test_conn_state_matrix_script_running(void)
{
    stub_reset_all();
    stub_script_running = true;

    /* TARGET/START/INTERVAL rejected with -911 (A1 matrix) */
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("CONN TARGET 180F", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "-911"));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("CONN START", resp, RESP_LEN));
    TEST_ASSERT_EQUAL_INT(CLI_ERR_STATE,
        cli_process_command("CONN INTERVAL 500", resp, RESP_LEN));

    /* STOP and STATUS remain available for recovery/observation */
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("CONN STATUS", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"cmd\":\"conn_status\""));
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("CONN STOP", resp, RESP_LEN));
}

static void test_status_includes_conn_object(void)
{
    stub_reset_all();
    TEST_ASSERT_EQUAL_INT(0, cli_process_command("STATUS", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "\"conn\":{"));
    TEST_ASSERT_TRUE(contains(resp, "\"enabled\":true"));
    TEST_ASSERT_TRUE(contains(resp, "\"state\":\"off\""));
}

static void test_conn_unknown_subcommand(void)
{
    stub_reset_all();
    TEST_ASSERT_EQUAL_INT(CLI_ERR_INVALID_CMD,
        cli_process_command("CONN FLY", resp, RESP_LEN));
    TEST_ASSERT_TRUE(contains(resp, "invalid syntax"));
}

/* ---- Runner ------------------------------------------------------------- */

int test_ble_conn_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_conn_merge_object);
    RUN_TEST(test_conn_envelope_keys_win);
    RUN_TEST(test_conn_wrap_non_object);
    RUN_TEST(test_conn_wrap_escapes);
    RUN_TEST(test_conn_merge_nested_values);
    RUN_TEST(test_conn_braces_inside_strings);
    RUN_TEST(test_conn_malformed_object_wraps);
    RUN_TEST(test_conn_truncation_marks_and_stays_valid);
    RUN_TEST(test_conn_empty_payload);
    RUN_TEST(test_conn_null_params);
    RUN_TEST(test_conn_out_len_matches);
    RUN_TEST(test_conn_bare_is_syntax_error);
    RUN_TEST(test_conn_status_off);
    RUN_TEST(test_conn_target_captures_uuids);
    RUN_TEST(test_conn_target_rejects_bad_uuid);
    RUN_TEST(test_conn_start_direct_captures_args);
    RUN_TEST(test_conn_start_relays_error_code);
    RUN_TEST(test_conn_stop_relays_not_connected);
    RUN_TEST(test_conn_interval_validation);
    RUN_TEST(test_conn_state_matrix_script_running);
    RUN_TEST(test_status_includes_conn_object);
    RUN_TEST(test_conn_unknown_subcommand);
    return UNITY_END();
}

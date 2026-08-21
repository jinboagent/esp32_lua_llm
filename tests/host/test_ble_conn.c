/*
 * Host tests for the F2.4 connection line encoder (json_encode_conn).
 * Covers the review-mandated behaviors: envelope-wins precedence (D2),
 * merge vs wrap, truncation marker (A5), escaping, buffer limits.
 */

#include "unity.h"
#include "json_if.h"

#include <stdio.h>
#include <string.h>

#define ADDR "AA:BB:CC:DD:EE:FF"

static void t_merge_clean_object(void)
{
    char buf[JSON_LINE_MAX_LEN];
    uint16_t len = 0;
    int r = json_encode_conn(123, ADDR, "{\"v\":42,\"name\":\"x\"}",
                             buf, sizeof(buf), &len);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":123,\"addr\":\"" ADDR "\",\"src\":\"conn\",\"v\":42,"
        "\"name\":\"x\"}", buf);
    TEST_ASSERT_EQUAL_UINT16(strlen(buf), len);
}

static void t_merge_empty_object(void)
{
    char buf[JSON_LINE_MAX_LEN];
    int r = json_encode_conn(1, ADDR, "{}", buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1,\"addr\":\"" ADDR "\",\"src\":\"conn\"}", buf);
}

static void t_envelope_wins_on_collision(void)
{
    char buf[JSON_LINE_MAX_LEN];
    int r = json_encode_conn(123, ADDR,
                             "{\"addr\":\"00:00:00:00:00:00\",\"v\":1}",
                             buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(0, r);
    /* wrapped: payload addr must NOT shadow the envelope addr */
    TEST_ASSERT_TRUE(strstr(buf, "\"src\":\"conn\",\"data\":\"") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\\\"addr\\\":\\\"00:00:00:00:00:00\\\"")
                     != NULL);
    TEST_ASSERT_TRUE(strncmp(buf, "{\"ts\":123,\"addr\":\"" ADDR "\"", 26) == 0);
}

static void t_nested_same_key_does_not_collide(void)
{
    char buf[JSON_LINE_MAX_LEN];
    int r = json_encode_conn(5, ADDR,
                             "{\"o\":{\"addr\":\"x\"},\"v\":2}",
                             buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(0, r);
    /* merged form: no "data" wrapper */
    TEST_ASSERT_TRUE(strstr(buf, "\"data\"") == NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\"o\":{\"addr\":\"x\"}") != NULL);
}

static void t_non_object_wrapped(void)
{
    char buf[JSON_LINE_MAX_LEN];
    int r = json_encode_conn(7, ADDR, "hello", buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":7,\"addr\":\"" ADDR "\",\"src\":\"conn\",\"data\":\"hello\"}",
        buf);
}

static void t_control_chars_escaped(void)
{
    char buf[JSON_LINE_MAX_LEN];
    int r = json_encode_conn(9, ADDR, "{\"a\":\"x\ny\tz\"}",
                             buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_TRUE(strstr(buf, "x\\ny\\tz") != NULL);
    TEST_ASSERT_TRUE(strchr(buf + 1, '\n') == NULL);  /* no raw newline */
}

static void t_truncation_marked(void)
{
    /* >450 bytes cannot fit the 512-byte line once the envelope and
     * "data" wrapper are paid for — forces the trunc path */
    char payload[600];
    memset(payload, 'A', sizeof(payload) - 1);
    payload[sizeof(payload) - 1] = '\0';

    char buf[JSON_LINE_MAX_LEN];
    uint16_t len = 0;
    int r = json_encode_conn(11, ADDR, payload, buf, sizeof(buf), &len);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_TRUE(len <= JSON_LINE_MAX_LEN - 1);
    TEST_ASSERT_TRUE(strstr(buf, "\"trunc\":true") != NULL);
    TEST_ASSERT_TRUE(buf[len] == '\0');
    TEST_ASSERT_TRUE(buf[strlen(buf) - 1] == '}');  /* valid JSON tail */
    /* envelope intact */
    TEST_ASSERT_TRUE(strncmp(buf, "{\"ts\":11,\"addr\":\"" ADDR "\"", 26) == 0);
}

static void t_small_buffer_rejected(void)
{
    char buf[8];
    int r = json_encode_conn(1, ADDR, "{\"v\":1}", buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(-203, r);
}

static void t_null_params_rejected(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL_INT(-202,
        json_encode_conn(1, NULL, "x", buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_INT(-202,
        json_encode_conn(1, ADDR, NULL, buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_INT(-202,
        json_encode_conn(1, ADDR, "x", NULL, sizeof(buf), NULL));
}

int test_ble_conn_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(t_merge_clean_object);
    RUN_TEST(t_merge_empty_object);
    RUN_TEST(t_envelope_wins_on_collision);
    RUN_TEST(t_nested_same_key_does_not_collide);
    RUN_TEST(t_non_object_wrapped);
    RUN_TEST(t_control_chars_escaped);
    RUN_TEST(t_truncation_marked);
    RUN_TEST(t_small_buffer_rejected);
    RUN_TEST(t_null_params_rejected);
    return UNITY_END();
}

#include "unity.h"
#include "json_if.h"
#include <string.h>
#include <stdio.h>

static proto_adv_report_t s_make_basic_report(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.ts_ms = 1000;
    r.addr[0] = 0xAA; r.addr[1] = 0xBB; r.addr[2] = 0xCC;
    r.addr[3] = 0xDD; r.addr[4] = 0xEE; r.addr[5] = 0xFF;
    r.addr_type = PROTO_ADDR_TYPE_PUBLIC;
    r.rssi = -45;
    strcpy(r.name, "Sensor_A");
    r.has_name = true;
    r.uuid16_list[0] = 0x180A;
    r.uuid16_count = 1;
    return r;
}

/* --- TC-1: Basic valid report --- */
void test_json_encode_basic(void)
{
    proto_adv_report_t r = s_make_basic_report();
    char buf[JSON_LINE_MAX_LEN];
    uint16_t len = 0;

    int ret = json_encode_adv(&r, buf, sizeof(buf), &len);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_GREATER_THAN(0, len);
    /* Verify it contains key fields */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":1000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"addr\":\"AA:BB:CC:DD:EE:FF\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"public\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rssi\":-45"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"Sensor_A\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"uuids\":[\"180A\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"manu\":null"));
}

/* --- TC-2: Report without name --- */
void test_json_encode_no_name(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.ts_ms = 500;
    r.rssi = -70;
    r.has_name = false;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":null"));
}

/* --- TC-3: Report with manufacturer data --- */
void test_json_encode_with_manufacturer(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.ts_ms = 2000;
    r.rssi = -30;
    r.has_manu = true;
    r.manu_id = 0x004C;
    r.manu_data[0] = 0xAB;
    r.manu_data[1] = 0xCD;
    r.manu_len = 2;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"manu\":{\"id\":\"004C\",\"data\":\"ABCD\"}"));
}

/* --- TC-4: Multiple UUIDs --- */
void test_json_encode_multiple_uuids(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.uuid16_list[0] = 0x180A;
    r.uuid16_list[1] = 0x180F;
    r.uuid16_list[2] = 0xFE95;
    r.uuid16_count = 3;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"uuids\":[\"180A\",\"180F\",\"FE95\"]"));
}

/* --- TC-5: Empty UUID array --- */
void test_json_encode_empty_uuids(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.uuid16_count = 0;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"uuids\":[]"));
}

/* --- TC-6: NULL report pointer --- */
void test_json_encode_null_report(void)
{
    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(NULL, buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(-202, ret);
}

/* --- TC-7: NULL buffer pointer --- */
void test_json_encode_null_buffer(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    int ret = json_encode_adv(&r, NULL, 512, NULL);
    TEST_ASSERT_EQUAL_INT(-202, ret);
}

/* --- TC-8: Buffer too small --- */
void test_json_encode_buffer_too_small(void)
{
    proto_adv_report_t r = s_make_basic_report();
    char buf[10];  /* Way too small */
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(-203, ret);
}

/* --- TC-9: Name with special characters (escaping) --- */
void test_json_encode_name_escaping(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.name, "Sen\"sor");
    r.has_name = true;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"Sen\\\"sor\""));
}

/* --- TC-10: Random address type --- */
void test_json_encode_random_addr(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.addr_type = PROTO_ADDR_TYPE_RANDOM;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"random\""));
}

/* --- TC-11: Output is valid JSON (starts with { ends with }) --- */
void test_json_encode_valid_json_structure(void)
{
    proto_adv_report_t r = s_make_basic_report();
    char buf[JSON_LINE_MAX_LEN];
    uint16_t len = 0;

    int ret = json_encode_adv(&r, buf, sizeof(buf), &len);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_CHAR('{', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('}', buf[len - 1]);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[len]);
}

/* --- TC-12: Name with newline/tab/control chars (B1 fix) --- */
void test_json_encode_name_newline_tab(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.name, "Dev\nTab\tEnd");
    r.has_name = true;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"Dev\\nTab\\tEnd\""));
}

/* --- TC-13: Name with control character < 0x20 (B1 fix) --- */
void test_json_encode_name_control_char(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.name[0] = 'A';
    r.name[1] = 0x01;
    r.name[2] = 'B';
    r.name[3] = '\0';
    r.has_name = true;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"A\\u01B\""));  /* \u01 = uppercase hex */
}

/* --- TC-14: Name with backslash (B1 fix) --- */
void test_json_encode_name_backslash(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.name, "path\\to");
    r.has_name = true;

    char buf[JSON_LINE_MAX_LEN];
    int ret = json_encode_adv(&r, buf, sizeof(buf), NULL);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"path\\\\to\""));
}

/* --- TC-15..19: json_escape_str (H1 fix) --- */

void test_json_escape_str_basic(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, json_escape_str("a\"b\\c\nd\te", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\nd\\te", out);
}

void test_json_escape_str_control_chars(void)
{
    char out[64];
    char in[] = {'A', 0x01, 'B', 0x1F, '\0'};
    TEST_ASSERT_EQUAL_INT(0, json_escape_str(in, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("A\\u0001B\\u001f", out);
}

void test_json_escape_str_lua_error_shape(void)
{
    /* The exact shape that broke host parsing pre-H1 */
    char out[128];
    TEST_ASSERT_EQUAL_INT(0, json_escape_str(
        "[string \"return +++\"]:1: unexpected symbol", out, sizeof(out)));
    TEST_ASSERT_NOT_NULL(strstr(out, "\\\"return +++\\\""));
}

void test_json_escape_str_truncation_stays_valid(void)
{
    char out[8];   /* room for 7 chars + NUL */
    /* ends with a char that needs a 2-char escape: the escape must not
     * be split across the buffer boundary */
    TEST_ASSERT_EQUAL_INT(0, json_escape_str("abcdef\"", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abcdef", out);  /* dangling escape dropped */

    char out2[4];
    TEST_ASSERT_EQUAL_INT(0, json_escape_str("a\"b", out2, sizeof(out2)));
    TEST_ASSERT_EQUAL_STRING("a\\\"", out2);   /* escape fits exactly */
}

void test_json_escape_str_null_params(void)
{
    char out[8];
    TEST_ASSERT_EQUAL_INT(-202, json_escape_str(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-202, json_escape_str("x", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-203, json_escape_str("x", out, 1));
    TEST_ASSERT_EQUAL_INT(-203, json_escape_str("x", out, 0));
}

int test_json_encoder_main(void);
int test_json_encoder_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_json_encode_basic);
    RUN_TEST(test_json_encode_no_name);
    RUN_TEST(test_json_encode_with_manufacturer);
    RUN_TEST(test_json_encode_multiple_uuids);
    RUN_TEST(test_json_encode_empty_uuids);
    RUN_TEST(test_json_encode_null_report);
    RUN_TEST(test_json_encode_null_buffer);
    RUN_TEST(test_json_encode_buffer_too_small);
    RUN_TEST(test_json_encode_name_escaping);
    RUN_TEST(test_json_encode_random_addr);
    RUN_TEST(test_json_encode_valid_json_structure);
    RUN_TEST(test_json_encode_name_newline_tab);
    RUN_TEST(test_json_encode_name_control_char);
    RUN_TEST(test_json_encode_name_backslash);
    RUN_TEST(test_json_escape_str_basic);
    RUN_TEST(test_json_escape_str_control_chars);
    RUN_TEST(test_json_escape_str_lua_error_shape);
    RUN_TEST(test_json_escape_str_truncation_stays_valid);
    RUN_TEST(test_json_escape_str_null_params);
    return UNITY_END();
}

#include "unity.h"
#include "proto_if.h"
#include <string.h>

/* --- TC-1: Valid advertisement with complete name + UUIDs + flags --- */
void test_parse_adv_valid_complete(void)
{
    uint8_t raw[] = {
        0x02, 0x01, 0x06,                                   /* Flags: LE General Discoverable */
        0x03, 0x03, 0x0A, 0x18,                             /* Complete UUID16: 0x180A (Device Info) */
        0x09, 0x09, 'S','e','n','s','o','r','_','A',        /* Complete Name: "Sensor_A" */
    };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(report.has_name);
    TEST_ASSERT_EQUAL_STRING("Sensor_A", report.name);
    TEST_ASSERT_EQUAL_UINT8(1, report.uuid16_count);
    TEST_ASSERT_EQUAL_HEX16(0x180A, report.uuid16_list[0]);
    TEST_ASSERT_TRUE(report.has_flags);
    TEST_ASSERT_EQUAL_HEX8(0x06, report.flags);
}

/* --- TC-2: Malformed AD — length field exceeds remaining bytes --- */
void test_parse_adv_malformed_length(void)
{
    uint8_t raw[] = {
        0x1F, 0x09, 0x41, 0x42   /* Claims 31 bytes of data, only 2 follow */
    };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(-104, ret);
}

/* --- TC-3: Empty payload (single zero byte) --- */
void test_parse_adv_empty_payload(void)
{
    uint8_t raw[] = { 0x00 };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_FALSE(report.has_name);
    TEST_ASSERT_EQUAL_UINT8(0, report.uuid16_count);
    TEST_ASSERT_FALSE(report.has_manu);
    TEST_ASSERT_FALSE(report.has_flags);
}

/* --- TC-4: Duplicate UUIDs — both preserved --- */
void test_parse_adv_duplicate_uuids(void)
{
    uint8_t raw[] = {
        0x07, 0x03, 0x0A, 0x18, 0x0A, 0x18, 0x0F, 0x18   /* 3 UUID16: 180A, 180A, 180F */
    };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT8(3, report.uuid16_count);
    TEST_ASSERT_EQUAL_HEX16(0x180A, report.uuid16_list[0]);
    TEST_ASSERT_EQUAL_HEX16(0x180A, report.uuid16_list[1]);
    TEST_ASSERT_EQUAL_HEX16(0x180F, report.uuid16_list[2]);
}

/* --- TC-5: NULL input pointer --- */
void test_parse_adv_null_raw(void)
{
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(NULL, 10, &report);
    TEST_ASSERT_EQUAL_INT(-102, ret);
}

/* --- TC-6: NULL output pointer --- */
void test_parse_adv_null_output(void)
{
    uint8_t raw[] = { 0x02, 0x01, 0x06 };
    int ret = proto_parse_adv_data(raw, sizeof(raw), NULL);
    TEST_ASSERT_EQUAL_INT(-102, ret);
}

/* --- TC-7: Zero length --- */
void test_parse_adv_zero_length(void)
{
    uint8_t raw[] = { 0x01 };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, 0, &report);
    TEST_ASSERT_EQUAL_INT(-103, ret);
}

/* --- TC-8: Manufacturer data parsing --- */
void test_parse_adv_manufacturer_data(void)
{
    uint8_t raw[] = {
        0x05, 0xFF, 0x4C, 0x00, 0x01, 0x02   /* Manufacturer: Apple (0x004C), data: 01 02 */
    };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(report.has_manu);
    TEST_ASSERT_EQUAL_HEX16(0x004C, report.manu_id);
    TEST_ASSERT_EQUAL_UINT8(2, report.manu_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, report.manu_data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, report.manu_data[1]);
}

/* --- TC-9: TX power level --- */
void test_parse_adv_tx_power(void)
{
    uint8_t raw[] = {
        0x02, 0x0A, 0xC8   /* TX Power: -56 dBm (0xC8 signed) */
    };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(report.has_tx_power);
    TEST_ASSERT_EQUAL_INT8(-56, report.tx_power);
}

/* --- TC-10: Name truncated at max length --- */
void test_parse_adv_name_max_length(void)
{
    uint8_t raw[31];
    raw[0] = 30;  /* length = 30 (type + 29 data bytes) */
    raw[1] = 0x09; /* Complete Name */
    memset(&raw[2], 'A', 29);  /* 29 'A' characters */

    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, 31, &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(report.has_name);
    TEST_ASSERT_EQUAL_UINT(29, strlen(report.name));  /* AD len=30: 1 type + 29 data */
    TEST_ASSERT_EQUAL_CHAR('A', report.name[0]);
}

/* --- TC-11: report_init --- */
void test_report_init(void)
{
    proto_adv_report_t report;
    memset(&report, 0xFF, sizeof(report));  /* fill with garbage */
    int ret = proto_report_init(&report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_FALSE(report.has_name);
    TEST_ASSERT_EQUAL_UINT8(0, report.uuid16_count);
    TEST_ASSERT_EQUAL_INT8(0, report.rssi);
}

/* --- TC-12: report_init NULL --- */
void test_report_init_null(void)
{
    int ret = proto_report_init(NULL);
    TEST_ASSERT_EQUAL_INT(-102, ret);
}

/* --- TC-13: Complex multi-field advertisement --- */
void test_parse_adv_complex(void)
{
    uint8_t raw[] = {
        0x02, 0x01, 0x06,                                   /* Flags */
        0x03, 0x03, 0x95, 0xFE,                             /* UUID: 0xFE95 (Xiaomi) */
        0x03, 0x03, 0x0A, 0x18,                             /* UUID: 0x180A (Device Info) */
        0x05, 0xFF, 0x4C, 0x00, 0xAB, 0xCD,                /* Manufacturer: Apple, data AB CD */
        0x02, 0x0A, 0xEC,                                   /* TX Power: -20 dBm */
        0x09, 0x09, 'T','e','s','t','D','e','v','1',        /* Name: "TestDev1" */
    };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(report.has_flags);
    TEST_ASSERT_EQUAL_HEX8(0x06, report.flags);
    TEST_ASSERT_EQUAL_UINT8(2, report.uuid16_count);
    TEST_ASSERT_EQUAL_HEX16(0xFE95, report.uuid16_list[0]);
    TEST_ASSERT_EQUAL_HEX16(0x180A, report.uuid16_list[1]);
    TEST_ASSERT_TRUE(report.has_manu);
    TEST_ASSERT_EQUAL_HEX16(0x004C, report.manu_id);
    TEST_ASSERT_TRUE(report.has_tx_power);
    TEST_ASSERT_EQUAL_INT8(-20, report.tx_power);
    TEST_ASSERT_TRUE(report.has_name);
    TEST_ASSERT_EQUAL_STRING("TestDev1", report.name);
}

/* --- TC-14: Shortened name (type 0x08) --- */
void test_parse_adv_shortened_name(void)
{
    uint8_t raw[] = {
        0x06, 0x08, 'S','e','n','s','o',   /* Shortened Name: "Senso" */
    };
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(raw, sizeof(raw), &report);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(report.has_name);
    TEST_ASSERT_EQUAL_STRING("Senso", report.name);
}

void test_adv_parser_main(void);
void test_adv_parser_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_adv_valid_complete);
    RUN_TEST(test_parse_adv_malformed_length);
    RUN_TEST(test_parse_adv_empty_payload);
    RUN_TEST(test_parse_adv_duplicate_uuids);
    RUN_TEST(test_parse_adv_null_raw);
    RUN_TEST(test_parse_adv_null_output);
    RUN_TEST(test_parse_adv_zero_length);
    RUN_TEST(test_parse_adv_manufacturer_data);
    RUN_TEST(test_parse_adv_tx_power);
    RUN_TEST(test_parse_adv_name_max_length);
    RUN_TEST(test_report_init);
    RUN_TEST(test_report_init_null);
    RUN_TEST(test_parse_adv_complex);
    RUN_TEST(test_parse_adv_shortened_name);
    UNITY_END();
}

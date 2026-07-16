#include "unity.h"
#include "filter_if.h"
#include <string.h>

static proto_adv_report_t s_make_report(const char *name, int8_t rssi,
                                         uint16_t uuid, const char *mac)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    if (name != NULL) {
        strncpy(r.name, name, PROTO_DEVICE_NAME_MAX_LEN - 1);
        r.has_name = true;
    }
    r.rssi = rssi;
    if (uuid != 0) {
        r.uuid16_list[0] = uuid;
        r.uuid16_count = 1;
    }
    if (mac != NULL) {
        /* parse "AA:BB:CC:DD:EE:FF" */
        for (int i = 0; i < 6; i++) {
            unsigned int byte;
            sscanf(mac + i * 3, "%02x", &byte);
            r.addr[i] = (uint8_t)byte;
        }
    }
    return r;
}

/* --- TC-1: No filters — all pass --- */
void test_filter_no_rules_passes_all(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    proto_adv_report_t r = s_make_report("Anything", -50, 0x180A, NULL);
    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r));
}

/* --- TC-2: Name filter exact match --- */
void test_filter_name_exact_match(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "Sensor_A", 0);

    proto_adv_report_t r1 = s_make_report("Sensor_A", -50, 0, NULL);
    proto_adv_report_t r2 = s_make_report("Sensor_B", -50, 0, NULL);

    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r1));
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r2));
}

/* --- TC-3: Name filter wildcard --- */
void test_filter_name_wildcard(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "Sensor*", 0);

    proto_adv_report_t r1 = s_make_report("Sensor_A", -50, 0, NULL);
    proto_adv_report_t r2 = s_make_report("SensorB", -50, 0, NULL);
    proto_adv_report_t r3 = s_make_report("Light_1", -50, 0, NULL);

    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r1));
    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r2));
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r3));
}

/* --- TC-4: Name filter case insensitive --- */
void test_filter_name_case_insensitive(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "sensor*", 0);

    proto_adv_report_t r = s_make_report("SENSOR_A", -50, 0, NULL);
    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r));
}

/* --- TC-5: UUID filter match --- */
void test_filter_uuid_match(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_UUID, "180A", 0);

    proto_adv_report_t r1 = s_make_report(NULL, -50, 0x180A, NULL);
    proto_adv_report_t r2 = s_make_report(NULL, -50, 0x180F, NULL);

    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r1));
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r2));
}

/* --- TC-6: RSSI threshold filter --- */
void test_filter_rssi_threshold(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_RSSI, NULL, -50);

    proto_adv_report_t r1 = s_make_report(NULL, -40, 0, NULL);  /* stronger than -50 */
    proto_adv_report_t r2 = s_make_report(NULL, -50, 0, NULL);  /* equal to -50 */
    proto_adv_report_t r3 = s_make_report(NULL, -60, 0, NULL);  /* weaker than -50 */

    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r1));
    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r2));
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r3));
}

/* --- TC-7: MAC address filter --- */
void test_filter_mac_match(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_MAC, "AA:BB:CC:DD:EE:FF", 0);

    proto_adv_report_t r1 = s_make_report(NULL, -50, 0, "AA:BB:CC:DD:EE:FF");
    proto_adv_report_t r2 = s_make_report(NULL, -50, 0, "11:22:33:44:55:66");

    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r1));
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r2));
}

/* --- TC-8: Multiple same-type filters (OR logic) --- */
void test_filter_same_type_or_logic(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "Sensor_A", 0);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "Sensor_B", 0);

    proto_adv_report_t r1 = s_make_report("Sensor_A", -50, 0, NULL);
    proto_adv_report_t r2 = s_make_report("Sensor_B", -50, 0, NULL);
    proto_adv_report_t r3 = s_make_report("Sensor_C", -50, 0, NULL);

    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r1));   /* matches first rule */
    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r2));   /* matches second rule */
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r3));  /* matches neither */
}

/* --- TC-9: Different-type filters (AND logic) --- */
void test_filter_different_type_and_logic(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "Sensor*", 0);
    filter_add_rule(&eng, FILTER_TYPE_RSSI, NULL, -50);

    proto_adv_report_t r1 = s_make_report("Sensor_A", -40, 0, NULL);  /* name OK, RSSI OK */
    proto_adv_report_t r2 = s_make_report("Sensor_A", -60, 0, NULL);  /* name OK, RSSI FAIL */
    proto_adv_report_t r3 = s_make_report("Light_1", -40, 0, NULL);   /* name FAIL, RSSI OK */

    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r1));
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r2));
    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r3));
}

/* --- TC-10: Filter clear --- */
void test_filter_clear(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "Sensor*", 0);
    TEST_ASSERT_EQUAL_INT(1, filter_get_count(&eng));

    filter_clear(&eng);
    TEST_ASSERT_EQUAL_INT(0, filter_get_count(&eng));

    /* After clear, everything should pass */
    proto_adv_report_t r = s_make_report("Anything", -90, 0, NULL);
    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r));
}

/* --- TC-11: Filter list full --- */
void test_filter_list_full(void)
{
    filter_engine_t eng;
    filter_init(&eng);

    for (int i = 0; i < FILTER_MAX_RULES; i++) {
        int ret = filter_add_rule(&eng, FILTER_TYPE_NAME, "test", 0);
        TEST_ASSERT_EQUAL_INT(0, ret);
    }

    /* Next add should fail */
    int ret = filter_add_rule(&eng, FILTER_TYPE_NAME, "overflow", 0);
    TEST_ASSERT_EQUAL_INT(-303, ret);
}

/* --- TC-12: NULL engine --- */
void test_filter_null_engine(void)
{
    TEST_ASSERT_EQUAL_INT(-302, filter_init(NULL));
    TEST_ASSERT_EQUAL_INT(-302, filter_clear(NULL));
    TEST_ASSERT_EQUAL_INT(-1, filter_get_count(NULL));
}

/* --- TC-13: Name filter on report without name --- */
void test_filter_name_no_name_in_report(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "Sensor*", 0);

    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    r.has_name = false;

    TEST_ASSERT_FALSE(filter_evaluate(&eng, &r));
}

/* --- TC-14: Wildcard at end matches everything --- */
void test_filter_wildcard_star_only(void)
{
    filter_engine_t eng;
    filter_init(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "*", 0);

    proto_adv_report_t r = s_make_report("AnyDevice", -50, 0, NULL);
    TEST_ASSERT_TRUE(filter_evaluate(&eng, &r));
}

void test_filter_engine_main(void);
void test_filter_engine_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_filter_no_rules_passes_all);
    RUN_TEST(test_filter_name_exact_match);
    RUN_TEST(test_filter_name_wildcard);
    RUN_TEST(test_filter_name_case_insensitive);
    RUN_TEST(test_filter_uuid_match);
    RUN_TEST(test_filter_rssi_threshold);
    RUN_TEST(test_filter_mac_match);
    RUN_TEST(test_filter_same_type_or_logic);
    RUN_TEST(test_filter_different_type_and_logic);
    RUN_TEST(test_filter_clear);
    RUN_TEST(test_filter_list_full);
    RUN_TEST(test_filter_null_engine);
    RUN_TEST(test_filter_name_no_name_in_report);
    RUN_TEST(test_filter_wildcard_star_only);
    UNITY_END();
}

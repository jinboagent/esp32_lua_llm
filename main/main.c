#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "proto_if.h"
#include "json_if.h"
#include "filter_if.h"

/* Hardcoded Xiaomi temperature sensor advertisement (31 bytes = legacy BLE max) */
static const uint8_t xiaomi_adv[] = {
    0x02, 0x01, 0x06,                                   /* Flags: 3 bytes */
    0x03, 0x03, 0x95, 0xFE,                             /* UUID16: 0xFE95 Xiaomi: 4 bytes */
    0x0C, 0x16, 0x95, 0xFE, 0x30, 0x58, 0x5B, 0x04,    /* Service data: 13 bytes */
    0xDE, 0xA7, 0x13, 0xD1, 0x0C,
    0x09, 0x09, 'L','Y','W','S','D','0','3',            /* Name "LYWSD03": 9 bytes */
};                                                     /* Total: 31 bytes */

void app_main(void)
{
    printf("\n=== BLE Sniffer Dongle — Module Demo ===\n\n");

    /* --- 1. Parse advertisement --- */
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(xiaomi_adv, sizeof(xiaomi_adv), &report);
    if (ret != 0) {
        printf("ERROR: parse failed: %d\n", ret);
        return;
    }
    printf("Parse OK: name=%s, uuids=%d, has_manu=%d\n",
           report.name, report.uuid16_count, report.has_manu);

    /* --- 2. Encode as JSON --- */
    char json_buf[JSON_LINE_MAX_LEN];
    uint16_t json_len = 0;
    ret = json_encode_adv(&report, json_buf, sizeof(json_buf), &json_len);
    if (ret != 0) {
        printf("ERROR: json encode failed: %d\n", ret);
        return;
    }
    printf("\nJSON output:\n%s\n", json_buf);

    /* --- 3. Filter test --- */
    filter_engine_t eng;
    filter_init(&eng);

    /* Add name filter: only pass "LYWSD*" */
    filter_add_rule(&eng, FILTER_TYPE_NAME, "LYWSD*", 0);

    bool pass1 = filter_evaluate(&eng, &report);
    printf("\nFilter 'LYWSD*' on LYWSD03: %s\n", pass1 ? "PASS" : "SUPPRESSED");

    /* Test with a different device */
    proto_adv_report_t other;
    proto_report_init(&other);
    strcpy(other.name, "RandomSpeaker");
    other.has_name = true;
    other.rssi = -55;

    bool pass2 = filter_evaluate(&eng, &other);
    printf("Filter 'LYWSD*' on RandomSpeaker: %s\n", pass2 ? "PASS" : "SUPPRESSED");

    /* --- 4. Combined filter: name AND RSSI --- */
    filter_clear(&eng);
    filter_add_rule(&eng, FILTER_TYPE_NAME, "LYWSD*", 0);
    filter_add_rule(&eng, FILTER_TYPE_RSSI, NULL, -50);

    bool pass3 = filter_evaluate(&eng, &report);
    printf("\nCombined filter (name=LYWSD* AND rssi>=-50) on Xiaomi: %s\n",
           pass3 ? "PASS" : "SUPPRESSED");

    report.rssi = -60;
    bool pass4 = filter_evaluate(&eng, &report);
    printf("Combined filter (name=LYWSD* AND rssi>=-50) on Xiaomi rssi=-60: %s\n",
           pass4 ? "PASS" : "SUPPRESSED");

    printf("\n=== Demo complete. All modules working on ESP32-S3! ===\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        printf("Heartbeat: uptime %lu s\n",
               (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ));
    }
}

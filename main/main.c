#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "proto_if.h"
#include "json_if.h"
#include "filter_if.h"

/* Test advertisement: flags + UUID16 (Xiaomi) + name "LYWSD03" */
static const uint8_t xiaomi_adv[] = {
    0x02, 0x01, 0x06,                       /* Flags: LE General Discoverable */
    0x03, 0x03, 0x95, 0xFE,                 /* UUID16: 0xFE95 (Xiaomi) */
    0x08, 0x09, 'L','Y','W','S','D','0','3' /* Complete Name: "LYWSD03" */
};

void app_main(void)
{
    printf("\n=== BLE Sniffer Dongle - Module Demo ===\n\n");

    /* 1. Parse advertisement using NimBLE ble_hs_adv_parse_fields() */
    proto_adv_report_t report;
    int ret = proto_parse_adv_data(xiaomi_adv, sizeof(xiaomi_adv), &report);
    if (ret != 0) {
        printf("ERROR: parse failed: %d\n", ret);
        return;
    }
    printf("Parse OK: name=%s, uuids=%d, has_manu=%d\n",
           report.name, report.uuid16_count, report.has_manu);

    /* 2. Encode as JSON using cJSON */
    char json_buf[JSON_LINE_MAX_LEN];
    uint16_t json_len = 0;
    ret = json_encode_adv(&report, json_buf, sizeof(json_buf), &json_len);
    if (ret != 0) {
        printf("ERROR: json encode failed: %d\n", ret);
        return;
    }
    printf("\nJSON output:\n%s\n", json_buf);

    /* 3. Filter tests */
    filter_engine_t eng;
    filter_init(&eng);

    filter_add_rule(&eng, FILTER_TYPE_NAME, "LYWSD*", 0);

    bool pass1 = filter_evaluate(&eng, &report);
    printf("\nFilter 'LYWSD*' on LYWSD03: %s\n", pass1 ? "PASS" : "SUPPRESSED");

    proto_adv_report_t other;
    proto_report_init(&other);
    strcpy(other.name, "RandomSpeaker");
    other.has_name = true;
    other.rssi = -55;

    bool pass2 = filter_evaluate(&eng, &other);
    printf("Filter 'LYWSD*' on RandomSpeaker: %s\n", pass2 ? "PASS" : "SUPPRESSED");

    /* 4. Combined filter: name AND RSSI */
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

#include "proto_if.h"
#include <string.h>

int proto_report_init(proto_adv_report_t *report)
{
    if (report == NULL) {
        return -102;
    }
    memset(report, 0, sizeof(proto_adv_report_t));
    return 0;
}

#ifdef ESP_PLATFORM
/*
 * ========================================================================
 *  ESP32 Implementation: NimBLE ble_hs_adv_parse_fields()
 *  Uses NimBLE's battle-tested AD parser. Handles all BLE-defined AD types.
 * ========================================================================
 */

#include "host/ble_hs.h"

int proto_parse_adv_data(const uint8_t *raw_data, uint16_t raw_len,
                         proto_adv_report_t *out)
{
    if (raw_data == NULL || out == NULL) {
        return -102;
    }
    if (raw_len == 0) {
        return -103;
    }

    /* Use NimBLE's built-in parser — handles all AD types, boundary-safe */
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    int rc = ble_hs_adv_parse_fields(&fields, raw_data, (uint8_t)raw_len);
    if (rc != 0) {
        return -104;
    }

    /* Initialize output struct */
    memset(out, 0, sizeof(*out));

    /* Flags */
    out->flags = fields.flags;
    out->has_flags = (fields.flags != 0);

    /* Device name (complete preferred over shortened) */
    if (fields.name != NULL && fields.name_len > 0) {
        uint8_t copy_len = fields.name_len;
        if (copy_len >= PROTO_DEVICE_NAME_MAX_LEN) {
            copy_len = PROTO_DEVICE_NAME_MAX_LEN - 1;
        }
        memcpy(out->name, fields.name, copy_len);
        out->name[copy_len] = '\0';
        out->has_name = true;
    }

    /* 16-bit Service UUIDs */
    if (fields.uuids16 != NULL) {
        for (uint8_t i = 0; i < fields.num_uuids16 && out->uuid16_count < PROTO_UUID16_MAX_COUNT; i++) {
            out->uuid16_list[out->uuid16_count] = ble_uuid_u16(&fields.uuids16[i].u);
            out->uuid16_count++;
        }
    }

    /* Manufacturer specific data (first 2 bytes = company ID) */
    if (fields.mfg_data != NULL && fields.mfg_data_len >= 2) {
        out->manu_id = fields.mfg_data[0] | ((uint16_t)fields.mfg_data[1] << 8);
        uint8_t data_len = fields.mfg_data_len - 2;
        if (data_len > PROTO_MANU_DATA_MAX_LEN) {
            data_len = PROTO_MANU_DATA_MAX_LEN;
        }
        memcpy(out->manu_data, &fields.mfg_data[2], data_len);
        out->manu_len = data_len;
        out->has_manu = true;
    }

    /* TX Power Level */
    if (fields.tx_pwr_lvl_is_present) {
        out->tx_power = fields.tx_pwr_lvl;
        out->has_tx_power = true;
    }

    return 0;
}

#else
/*
 * ========================================================================
 *  Host Test Implementation: Simple AD parser for unit testing
 *  Used when compiling on PC (no NimBLE available).
 *  Same logic as the original parser, for test compatibility.
 * ========================================================================
 */

#define AD_TYPE_FLAGS                   0x01
#define AD_TYPE_INCOMPLETE_UUID16       0x02
#define AD_TYPE_COMPLETE_UUID16         0x03
#define AD_TYPE_SHORTENED_NAME          0x08
#define AD_TYPE_COMPLETE_NAME           0x09
#define AD_TYPE_TX_POWER_LEVEL          0x0A
#define AD_TYPE_MANUFACTURER_DATA       0xFF

static int s_parse_name(proto_adv_report_t *out, const uint8_t *data, uint8_t len)
{
    uint8_t copy_len = len;
    if (copy_len >= PROTO_DEVICE_NAME_MAX_LEN) {
        copy_len = PROTO_DEVICE_NAME_MAX_LEN - 1;
    }
    memcpy(out->name, data, copy_len);
    out->name[copy_len] = '\0';
    out->has_name = true;
    return 0;
}

static int s_parse_uuid16_list(proto_adv_report_t *out, const uint8_t *data, uint8_t len)
{
    uint8_t count = len / 2;
    for (uint8_t i = 0; i < count && out->uuid16_count < PROTO_UUID16_MAX_COUNT; i++) {
        uint16_t uuid = (uint16_t)data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8);
        out->uuid16_list[out->uuid16_count] = uuid;
        out->uuid16_count++;
    }
    return 0;
}

static int s_parse_manufacturer_data(proto_adv_report_t *out, const uint8_t *data, uint8_t len)
{
    if (len < 2) {
        return 0;
    }
    out->manu_id = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    uint8_t data_len = len - 2;
    if (data_len > PROTO_MANU_DATA_MAX_LEN) {
        data_len = PROTO_MANU_DATA_MAX_LEN;
    }
    memcpy(out->manu_data, &data[2], data_len);
    out->manu_len = data_len;
    out->has_manu = true;
    return 0;
}

int proto_parse_adv_data(const uint8_t *raw_data, uint16_t raw_len,
                         proto_adv_report_t *out)
{
    if (raw_data == NULL || out == NULL) {
        return -102;
    }
    if (raw_len == 0) {
        return -103;
    }

    proto_adv_report_t temp;
    memset(&temp, 0, sizeof(temp));

    uint16_t pos = 0;
    while (pos < raw_len) {
        uint8_t field_len = raw_data[pos];
        if (field_len == 0) {
            break;
        }

        if (pos + field_len >= raw_len) {
            return -104;
        }

        uint8_t ad_type = raw_data[pos + 1];
        const uint8_t *ad_data = &raw_data[pos + 2];
        uint8_t ad_data_len = field_len - 1;

        switch (ad_type) {
        case AD_TYPE_FLAGS:
            if (ad_data_len >= 1) {
                temp.flags = ad_data[0];
                temp.has_flags = true;
            }
            break;

        case AD_TYPE_INCOMPLETE_UUID16:
        case AD_TYPE_COMPLETE_UUID16:
            s_parse_uuid16_list(&temp, ad_data, ad_data_len);
            break;

        case AD_TYPE_SHORTENED_NAME:
        case AD_TYPE_COMPLETE_NAME:
            if (!temp.has_name) {
                s_parse_name(&temp, ad_data, ad_data_len);
            }
            break;

        case AD_TYPE_TX_POWER_LEVEL:
            if (ad_data_len >= 1) {
                temp.tx_power = (int8_t)ad_data[0];
                temp.has_tx_power = true;
            }
            break;

        case AD_TYPE_MANUFACTURER_DATA:
            if (!temp.has_manu) {
                s_parse_manufacturer_data(&temp, ad_data, ad_data_len);
            }
            break;

        default:
            break;
        }

        pos += field_len + 1;
    }

    *out = temp;
    return 0;
}

#endif /* ESP_PLATFORM */

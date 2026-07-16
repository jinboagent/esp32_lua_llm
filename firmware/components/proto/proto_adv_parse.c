#include "proto_if.h"
#include <string.h>

/* AD type constants */
#define AD_TYPE_FLAGS                   0x01
#define AD_TYPE_INCOMPLETE_UUID16       0x02
#define AD_TYPE_COMPLETE_UUID16         0x03
#define AD_TYPE_INCOMPLETE_UUID32       0x04
#define AD_TYPE_COMPLETE_UUID32         0x05
#define AD_TYPE_INCOMPLETE_UUID128      0x06
#define AD_TYPE_COMPLETE_UUID128        0x07
#define AD_TYPE_SHORTENED_NAME          0x08
#define AD_TYPE_COMPLETE_NAME           0x09
#define AD_TYPE_TX_POWER_LEVEL          0x0A
#define AD_TYPE_MANUFACTURER_DATA       0xFF

int proto_report_init(proto_adv_report_t *report)
{
    if (report == NULL) {
        return -102;
    }
    memset(report, 0, sizeof(proto_adv_report_t));
    return 0;
}

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

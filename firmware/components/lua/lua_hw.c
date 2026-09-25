/*
 * hw.* bindings for the Lua sandbox (H6.1 M3) — the L0 device API.
 *
 * Behind CONFIG_LUA_HW_BINDINGS (default on for this product build; a
 * locked-down build compiles them out entirely and the tools that wrap
 * them degrade to clean error strings). Bindings are thin ESP-IDF
 * wrappers; the sandbox whitelist, the per-line scan and the engine
 * budget remain the gates above them.
 *
 *   hw.millis()                -> ms since boot (esp_timer)
 *   hw.gpio_write(pin, 0|1)    -> true          (whitelisted pins)
 *   hw.gpio_read(pin)          -> 0|1 level
 *   hw.adc_read(pin)           -> raw 12-bit    (ADC1: gpio 1..10 on S3)
 *   hw.kv_set(key, value)      -> true          (files under /littlefs/kv)
 *   hw.kv_get(key)             -> string | nil
 *
 * kv is decision 14's `configure` seam made real: the EFFECT (the
 * parameters) persists on the device and survives power cycles; the
 * script that set it does not.
 */
#include "lua_hw.h"
#include "lua_if.h"
#include "sdkconfig.h"

#if CONFIG_LUA_HW_BINDINGS

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "lua.h"
#include "lauxlib.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "storage_if.h"

#define HW_KV_DIR      "/littlefs/kv"
#define HW_KV_MAX_KEY  24
#define HW_KV_MAX_VAL  96

/* ESP32-S3 pin whitelist: 19/20 are USB D+/D-, 26-32 are flash, 33-37
 * are octal PSRAM on R8 modules — none may be touched from Lua.
 * Also excluded (audit B12): 3 and 46 are JTAG-source/boot-msg
 * strapping pins, 45 is the VDD_SPI voltage strap, 43/44 are UART0
 * TX/RX (the ROM loader's emergency console) — persistent autorun
 * packs must not be able to rewire the boot/panic paths. */
static bool s_pin_ok(int pin)
{
    if (pin == 3 || pin == 45 || pin == 46 ||
        pin == 43 || pin == 44)
        return false;
    return (pin >= 1 && pin <= 18) || (pin >= 21 && pin <= 25) ||
           (pin >= 38 && pin <= 48);
}

/* Per-pin direction cache so a read never reconfigures a pin we drove
 * (reading an output returns its driven level) and vice versa. */
static uint64_t s_out_mask = 0;
static uint64_t s_in_mask = 0;

static void s_config_pin(int pin, bool output)
{
    uint64_t bit = (uint64_t)1 << pin;
    if (output) {
        if (!(s_out_mask & bit)) {
            gpio_reset_pin((gpio_num_t)pin);
            /* INPUT_OUTPUT so a written level can be read back
             * (pure OUTPUT keeps the input buffer off — caught live by
             * the hw suite 2026-09-07) */
            gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT_OUTPUT);
            s_out_mask |= bit;
            s_in_mask &= ~bit;
        }
    } else {
        if (!(s_in_mask & bit) && !(s_out_mask & bit)) {
            gpio_reset_pin((gpio_num_t)pin);
            gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
            s_in_mask |= bit;
        }
    }
}

static bool s_key_ok(const char *k)
{
    size_t n = strlen(k);
    if (n == 0 || n > HW_KV_MAX_KEY)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}

static int lhw_millis(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)(esp_timer_get_time() / 1000));
    return 1;
}

static int lhw_gpio_write(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (!s_pin_ok(pin))
        return luaL_error(L, "hw.gpio_write: pin %d is not whitelisted", pin);
    s_config_pin(pin, true);
    gpio_set_level((gpio_num_t)pin, val ? 1 : 0);
    lua_pushboolean(L, 1);
    return 1;
}

static int lhw_gpio_read(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    if (!s_pin_ok(pin))
        return luaL_error(L, "hw.gpio_read: pin %d is not whitelisted", pin);
    s_config_pin(pin, false);
    lua_pushinteger(L, gpio_get_level((gpio_num_t)pin));
    return 1;
}

static adc_oneshot_unit_handle_t s_adc1;

static int lhw_adc_read(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    if (pin < 1 || pin > 10)
        return luaL_error(L, "hw.adc_read: ADC1 covers gpio 1..10 on S3");
    if (s_adc1 == NULL) {
        adc_oneshot_unit_init_cfg_t cfg = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        if (adc_oneshot_new_unit(&cfg, &s_adc1) != ESP_OK)
            return luaL_error(L, "hw.adc_read: ADC init failed");
    }
    /* S3: gpio 1..10 map to ADC1 channels 0..9 */
    int raw = -1;
    if (adc_oneshot_read(s_adc1, (adc_channel_t)(pin - 1), &raw) != ESP_OK)
        return luaL_error(L, "hw.adc_read: read failed");
    lua_pushinteger(L, raw);
    return 1;
}

static int lhw_kv_set(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    if (!s_key_ok(k))
        return luaL_error(L,
            "hw.kv_set: key must be <=24 chars of [A-Za-z0-9_.-]");
    char numbuf[32];
    const char *v = NULL;
    if (lua_isboolean(L, 2)) {
        snprintf(numbuf, sizeof(numbuf), "%s",
                 lua_toboolean(L, 2) ? "true" : "false");
        v = numbuf;
    } else if (lua_isstring(L, 2) || lua_isnumber(L, 2)) {
        v = lua_tostring(L, 2);
    } else {
        return luaL_error(L, "hw.kv_set: value must be string/number/bool");
    }
    if (strlen(v) > HW_KV_MAX_VAL)
        return luaL_error(L, "hw.kv_set: value too long (<=96)");
    char path[STORAGE_MAX_PATH_LEN];
    snprintf(path, sizeof(path), HW_KV_DIR "/%s", k);
    int ret = storage_write_file(path, (const uint8_t *)v, strlen(v));
    if (ret != 0)
        return luaL_error(L, "hw.kv_set: storage failed (%d)", ret);
    lua_pushboolean(L, 1);
    return 1;
}

static int lhw_kv_get(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    if (!s_key_ok(k))
        return luaL_error(L, "hw.kv_get: bad key");
    char path[STORAGE_MAX_PATH_LEN];
    snprintf(path, sizeof(path), HW_KV_DIR "/%s", k);
    char val[HW_KV_MAX_VAL + 1];
    uint32_t len = 0;
    if (storage_read_file(path, (uint8_t *)val, sizeof(val) - 1, &len) != 0) {
        lua_pushnil(L);            /* absent key: nil, never an error */
        return 1;
    }
    lua_pushlstring(L, val, len);
    return 1;
}

void lua_hw_register(lua_State *L)
{
    mkdir(HW_KV_DIR, 0775);        /* idempotent */
    lua_newtable(L);
    lua_pushcfunction(L, lhw_millis);
    lua_setfield(L, -2, "millis");
    lua_pushcfunction(L, lhw_gpio_write);
    lua_setfield(L, -2, "gpio_write");
    lua_pushcfunction(L, lhw_gpio_read);
    lua_setfield(L, -2, "gpio_read");
    lua_pushcfunction(L, lhw_adc_read);
    lua_setfield(L, -2, "adc_read");
    lua_pushcfunction(L, lhw_kv_set);
    lua_setfield(L, -2, "kv_set");
    lua_pushcfunction(L, lhw_kv_get);
    lua_setfield(L, -2, "kv_get");
    lua_setglobal(L, "hw");
    printf("Lua: hw.* bindings registered (gpio/adc/millis/kv)\n");
}

#else /* !CONFIG_LUA_HW_BINDINGS */

void lua_hw_register(lua_State *L)
{
    (void)L;                       /* compiled out: the sandbox stays pure */
}

#endif

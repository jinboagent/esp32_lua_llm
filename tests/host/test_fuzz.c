/*
 * Host tests: fuzz + boundary + scanner-corpus hardening (2026-08-29).
 *
 * Three ideas, one file:
 *  A) Encoder round-trips — randomized advertisement reports and conn
 *     payloads (quotes, backslashes, control bytes, UTF-8, valid JSON
 *     objects, non-objects) go through json_encode_adv /
 *     json_encode_conn; whenever the encoder returns 0 the output MUST
 *     be strict valid JSON (tv_json.h). Deterministic LCG — a failure
 *     always reproduces from the printed seed.
 *  B) Budget boundaries — the conn encoder's staged wrap-mode budgets
 *     (close the data string, close the object) are probed at
 *     cap-1/cap/cap+1 sizes: rc==0 always means valid JSON; -203 means
 *     the output is unusable and its bytes are not asserted.
 *  C) Bridge scanner corpus — the fail-closed sandbox scan's actual
 *     policy, asserted line by line: dotted tokens (os./io./debug./
 *     package.) match ANYWHERE (inside strings and comments too);
 *     word tokens (dofile/loadfile/load/require/collectgarbage) match
 *     only at identifier boundaries; every rejection is valid JSON
 *     carrying -612; clean lines are acked silently (empty response).
 *     The split-evasion pair (os / .time() on separate lines) is
 *     documented as accepted-by-scan: the sandbox has no os library,
 *     so the assembled call fails at runtime inside the engine's error
 *     handling — the scan is the first gate, not the only one.
 */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "unity.h"
#include "json_if.h"
#include "proto_if.h"
#include "bridge_if.h"
#include "script_if.h"
#include "test_stubs.h"
#include "tv_json.h"

/* ---- Deterministic randomness (LCG) -------------------------------------- */

static uint32_t rng_state;
static void rng_seed(uint32_t s) { rng_state = s ? s : 1; }
static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}
static uint32_t rng_below(uint32_t n) { return rng_next() % n; }

/* ---- A) Encoder round-trips ---------------------------------------------- */

static char out[JSON_LINE_MAX_LEN + 128];

static void fill_random_report(proto_adv_report_t *r)
{
    memset(r, 0, sizeof(*r));
    for (int i = 0; i < PROTO_ADDR_LEN; i++) {
        r->addr[i] = (uint8_t)rng_below(256);
    }
    r->addr_type = (proto_addr_type_t)rng_below(2);
    r->rssi = (int8_t)rng_below(256) - 128;
    r->ts_ms = rng_next();
    r->has_name = rng_below(2);
    if (r->has_name) {
        /* name bytes from a nasty pool: quotes, backslash, controls,
         * high UTF-8, plain text */
        static const char pool[] =
            "abZ0-_. \t\"\\\x01\x1f\x7f\xc3\xa9\xe4\xb8\xad";
        size_t pool_len = sizeof(pool) - 1;
        size_t n = rng_below(PROTO_DEVICE_NAME_MAX_LEN - 1);
        for (size_t i = 0; i < n; i++) {
            r->name[i] = pool[rng_below(pool_len)];
        }
        r->name[n] = '\0';
    }
    r->uuid16_count = (uint8_t)rng_below(PROTO_UUID16_MAX_COUNT + 1);
    for (int i = 0; i < r->uuid16_count; i++) {
        r->uuid16_list[i] = (uint16_t)rng_below(0x10000);
    }
    r->has_manu = rng_below(2);
    if (r->has_manu) {
        r->manu_id = (uint16_t)rng_below(0x10000);
        r->manu_len = (uint8_t)rng_below(PROTO_MANU_DATA_MAX_LEN + 1);
        for (int i = 0; i < r->manu_len; i++) {
            r->manu_data[i] = (uint8_t)rng_below(256);
        }
    }
    r->has_tx_power = rng_below(2);
    r->tx_power = (int8_t)rng_below(256) - 128;
    r->has_flags = rng_below(2);
    r->flags = (uint8_t)rng_below(256);
}

static void test_adv_encoder_random_roundtrip(void)
{
    uint32_t seed = 0xC0FFEE1u;
    rng_seed(seed);
    proto_adv_report_t r;
    uint16_t out_len = 0;
    for (int i = 0; i < 500; i++) {
        fill_random_report(&r);
        int rc = json_encode_adv(&r, out, sizeof(out), &out_len);
        if (rc == 0) {
            if (!tvj_valid(out)) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "iter %d (seed %#x): invalid JSON: %.90s",
                         i, seed, out);
                TEST_FAIL_MESSAGE(msg);
            }
        } else if (rc != -203) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "iter %d (seed %#x): rc %d", i, seed, rc);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

static void test_conn_encoder_random_bytes_roundtrip(void)
{
    uint32_t seed = 0xBADF00Du;
    rng_seed(seed);
    uint8_t payload[300];
    uint16_t out_len = 0;
    for (int i = 0; i < 500; i++) {
        uint16_t n = (uint16_t)rng_below(sizeof(payload) + 1);
        /* mix: raw random bytes + a nasty printable pool */
        static const char pool[] = "ab{}[]:,\"\\\n\r\t\x01\xc3\xa9";
        size_t pool_len = sizeof(pool) - 1;
        for (uint16_t j = 0; j < n; j++) {
            payload[j] = rng_below(2)
                ? (uint8_t)rng_below(256)
                : (uint8_t)pool[rng_below(pool_len)];
        }
        int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 1234,
                                  payload, n, out, sizeof(out), &out_len);
        if (rc == 0 && !tvj_valid(out)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "iter %d len %d (seed %#x): invalid JSON: %.90s",
                     i, n, seed, out);
            TEST_FAIL_MESSAGE(msg);
        }
        /* rc -203 (truncated) is acceptable; anything else is not */
        if (rc != 0 && rc != -203) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "iter %d (seed %#x): rc %d", i, seed, rc);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

static void test_conn_encoder_random_json_payloads(void)
{
    /* payload = valid JSON objects with nasty members: must MERGE and
     * the result stays valid JSON; payload = valid non-objects
     * (numbers/strings/arrays): must WRAP and stay valid */
    static const char *objs[] = {
        "{\"t\":1,\"u\":0,\"y\":0.5}",
        "{\"s\":\"quote\\\" back\\\\slash nl\\n\"}",
        "{\"nested\":{\"a\":[1,2,{\"b\":null}]},\"ok\":true}",
        "{\"ts\":99,\"addr\":\"XX\",\"src\":\"fake\",\"keep\":7}",
        "{\"utf8\":\"\xc3\xa9\xe4\xb8\xad\"}",      /* raw UTF-8 bytes */
        "{\"bad_esc\":\"\\x41\"}",   /* \x is NOT a JSON escape: merge must
                                        reject it and fall back to wrapping */
        "{}",
    };
    static const char *wraps[] = {
        "hello 25.5", "3.14", "[1,2,3]", "true", "null",
        "a\"b\\c\nd", "",
    };
    uint16_t out_len = 0;
    for (size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); i++) {
        int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 5,
                                  (const uint8_t *)objs[i],
                                  (uint16_t)strlen(objs[i]),
                                  out, sizeof(out), &out_len);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, objs[i]);
        if (!tvj_valid(out)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "merge payload invalid: %.100s",
                     out);
            TEST_FAIL_MESSAGE(msg);
        }
    }
    for (size_t i = 0; i < sizeof(wraps) / sizeof(wraps[0]); i++) {
        int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 5,
                                  (const uint8_t *)wraps[i],
                                  (uint16_t)strlen(wraps[i]),
                                  out, sizeof(out), &out_len);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, wraps[i]);
        if (!tvj_valid(out)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "wrap payload invalid: %.100s",
                     out);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

/* ---- B) Budget boundaries ------------------------------------------------ */

static void test_conn_encoder_invalid_escape_falls_back_to_wrap(void)
{
    /* \x is not a JSON escape; the merge scanner must reject the member
     * (verbatim copy would emit invalid JSON) and the wrap fallback
     * must escape the backslash safely (2026-08-29 fuzz finding). */
    static const char payload[] = "{\"a\":\"\\x41\",\"b\":2}";
    uint16_t out_len = 0;
    int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 9,
                              (const uint8_t *)payload,
                              (uint16_t)strlen(payload),
                              out, sizeof(out), &out_len);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(tvj_valid(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"data\""));   /* wrapped */
    TEST_ASSERT_NULL(strstr(out, "\"a\":"));         /* not merged */
}

static void test_conn_encoder_budget_boundaries(void)
{
    /* Probe every small buffer size: whenever the encoder reports
     * success the bytes must parse — the staged budgets guarantee the
     * string and object can always close. */
    char small[160];
    static const char payload[] =
        "{\"t\":12.345,\"u\":1,\"y\":0.6789}";
    uint16_t out_len = 0;
    for (size_t cap = 2; cap < sizeof(small); cap++) {
        int rc = json_encode_conn("AA:BB:CC:DD:EE:FF", 123456,
                                  (const uint8_t *)payload,
                                  (uint16_t)(strlen(payload)),
                                  small, (uint16_t)cap, &out_len);
        if (rc == 0 && !tvj_valid(small)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "cap %zu: rc 0 but invalid JSON: %.90s", cap, small);
            TEST_FAIL_MESSAGE(msg);
        }
        if (rc != 0 && rc != -203) {
            char msg[96];
            snprintf(msg, sizeof(msg), "cap %zu: rc %d", cap, rc);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

static void test_adv_encoder_tiny_buffers_never_emit_garbage(void)
{
    proto_adv_report_t r;
    memset(&r, 0, sizeof(r));
    memcpy(r.addr, "\x11\x22\x33\x44\x55\x66", 6);
    r.rssi = -70;
    r.ts_ms = 1234;
    r.has_name = true;
    strcpy(r.name, "Sensor#1 \"quoted\"");
    char small[64];
    uint16_t out_len = 0;
    for (size_t cap = 2; cap < sizeof(small); cap++) {
        int rc = json_encode_adv(&r, small, (uint16_t)cap, &out_len);
        if (rc == 0 && !tvj_valid(small)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "cap %zu: rc 0 but invalid JSON: %.60s", cap, small);
            TEST_FAIL_MESSAGE(msg);
        }
        if (rc != 0 && rc != -203) {
            char msg[96];
            snprintf(msg, sizeof(msg), "cap %zu: rc %d", cap, rc);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

/* ---- C) Bridge scanner corpus -------------------------------------------- */

#define BRESP_LEN 256
static char bresp[BRESP_LEN];

/* Submit one line during an upload; returns the bridge rc. resp[0]
 * stays '\0' for the silent clean-line ack. */
static int bridge_line(const char *line)
{
    bresp[0] = '\0';
    return bridge_handle_script_upload(line, strlen(line),
                                       bresp, BRESP_LEN);
}

static void upload_begin(void)
{
    bridge_abort();
    TEST_ASSERT_EQUAL_INT(0, bridge_upload_begin(bresp, BRESP_LEN));
    TEST_ASSERT_TRUE(bridge_is_uploading());
}

static void expect_reject(const char *line, const char *token)
{
    /* fail-closed: every rejection resets the upload session, so each
     * probe needs a fresh begin */
    upload_begin();
    int rc = bridge_line(line);
    char msg[160];
    snprintf(msg, sizeof(msg), "line '%s' should be rejected", line);
    TEST_ASSERT_EQUAL_INT_MESSAGE(BRIDGE_ERR_REJECTED, rc, msg);
    snprintf(msg, sizeof(msg), "rejection for '%s' must be valid JSON",
             line);
    TEST_ASSERT_TRUE_MESSAGE(tvj_valid(bresp), msg);
    snprintf(msg, sizeof(msg), "rejection for '%s' must carry -612", line);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(bresp, "\"code\":-612"), msg);
    snprintf(msg, sizeof(msg), "rejection for '%s' must name '%s'",
             line, token);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(bresp, token), msg);
    /* fail-closed: the session is gone */
    TEST_ASSERT_FALSE_MESSAGE(bridge_is_uploading(),
                              "rejection must reset the upload session");
}

static void expect_accept(const char *line)
{
    int rc = bridge_line(line);
    char msg[160];
    snprintf(msg, sizeof(msg), "line '%s' should be accepted", line);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, msg);
    /* clean data lines are acked silently — nothing but a valid JSON
     * response would also be fine; empty is the contract */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, strlen(bresp),
                                      "clean line must ack silently");
    TEST_ASSERT_TRUE_MESSAGE(bridge_is_uploading(),
                             "clean line must keep the session");
}

static void test_bridge_scanner_corpus(void)
{
    /* dotted tokens: matched anywhere, including strings/comments —
     * one fresh upload per probe (fail-closed resets the session) */
    expect_reject("local t = os.time()", "os.");
    expect_reject("io.write('x')", "io.");
    expect_reject("debug.traceback()", "debug.");
    expect_reject("package.loaded", "package.");
    expect_reject("local s = 'calls os.time() inside'", "os.");
    expect_reject("-- comment mentions io.write", "io.");

    /* word tokens: identifier-boundary matching */
    expect_reject("require('x')", "require");
    expect_reject("dofile('a')", "dofile");
    expect_reject("loadfile('a')", "loadfile");
    expect_reject("local f = load('return 1')", "load");
    expect_reject("collectgarbage()", "collectgarbage");
    /* boundary false-positives must NOT trigger: 'loadx', 'myrequire',
     * 'load' inside a longer identifier are not the token (fresh
     * session — the rejects above reset it) */
    upload_begin();
    expect_accept("local loadx = 1");
    expect_accept("local myrequire = 2");
    expect_accept("-- requires two arguments");

    /* clean lines pass silently */
    upload_begin();
    expect_accept("function on_adv(addr, t, rssi, name, uuids, mi, md)");
    expect_accept("  return rssi >= -75 and name ~= nil");
    expect_accept("end");
    expect_accept("local s = 'os time'");       /* 'os' without dot */
    expect_accept("local x = os");              /* no dot: scan passes */
    expect_accept(".time()");                   /* split-evasion other half */
    TEST_ASSERT_EQUAL_INT(0, bridge_upload_finish(bresp, BRESP_LEN));

    /* documented policy note (see file header): the split pair
     * 'local x = os' + '.time()' passes the textual scan; the sandbox
     * itself has no os library, so the assembled call fails at runtime
     * inside the engine's per-hook error handling. The scan is the
     * first gate, not the only one. */
}

static void test_bridge_rejection_responses_valid_json(void)
{
    /* -612 fires mid-upload with a JSON body; the session reset means
     * the NEXT line (previously 'no upload in progress' -611) is also
     * JSON — both validated, in order (the 2026-08-28 host-tool lesson:
     * never let -611 mask the real -612). */
    upload_begin();
    expect_reject("local t = os.time()", "os.");
    int rc = bridge_line("print('after')");
    TEST_ASSERT_EQUAL_INT(BRIDGE_ERR_STATE, rc);
    TEST_ASSERT_TRUE(tvj_valid(bresp));
    TEST_ASSERT_NOT_NULL(strstr(bresp, "\"code\":-611"));
}

/* ---- Entry point --------------------------------------------------------- */

int test_fuzz_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_adv_encoder_random_roundtrip);
    RUN_TEST(test_conn_encoder_random_bytes_roundtrip);
    RUN_TEST(test_conn_encoder_random_json_payloads);
    RUN_TEST(test_conn_encoder_invalid_escape_falls_back_to_wrap);
    RUN_TEST(test_conn_encoder_budget_boundaries);
    RUN_TEST(test_adv_encoder_tiny_buffers_never_emit_garbage);
    RUN_TEST(test_bridge_scanner_corpus);
    RUN_TEST(test_bridge_rejection_responses_valid_json);

    return UNITY_END();
}

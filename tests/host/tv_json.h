/*
 * test-support: a strict JSON validator for host tests.
 *
 * Shared by test_cli_responses.c (every CLI response must be valid
 * JSON — the 2026-08-28 CONN TARGET bug shipped invalid JSON for weeks
 * because nothing checked) and test_fuzz.c (encoder outputs must
 * round-trip). Header-only static functions; include once per test
 * file. Strictness: RFC 8259 objects/arrays/strings/numbers/literals,
 * no leading zeros, no trailing garbage. Bytes >= 0x80 inside strings
 * are accepted as opaque UTF-8 (the firmware passes UTF-8 through;
 * full Unicode validation is out of scope here).
 */
#ifndef TV_JSON_H
#define TV_JSON_H

#include <stdbool.h>
#include <string.h>

static bool tvj_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') {
        (*p)++;
    }
    return true;
}

static bool tvj_string(const char **p)
{
    if (**p != '"') return false;
    (*p)++;
    for (;;) {
        char c = **p;
        if (c == '\0') return false;           /* unterminated */
        if (c == '"') { (*p)++; return true; }
        if (c == '\\') {
            (*p)++;
            char e = **p;
            if (e == '"' || e == '\\' || e == '/' || e == 'b' ||
                e == 'f' || e == 'n' || e == 'r' || e == 't') {
                (*p)++;
            } else if (e == 'u') {
                (*p)++;
                for (int i = 0; i < 4; i++, (*p)++) {
                    char h = **p;
                    bool hex = (h >= '0' && h <= '9') ||
                               (h >= 'a' && h <= 'f') ||
                               (h >= 'A' && h <= 'F');
                    if (!hex) return false;
                }
            } else {
                return false;                   /* bad escape */
            }
        } else if ((unsigned char)c < 0x20) {
            return false;                       /* raw control char */
        } else {
            (*p)++;                             /* printable or UTF-8 */
        }
    }
}

static bool tvj_number(const char **p)
{
    if (**p == '-') (*p)++;
    if (**p == '0') {
        (*p)++;
    } else if (**p >= '1' && **p <= '9') {
        while (**p >= '0' && **p <= '9') (*p)++;
    } else {
        return false;
    }
    if (**p == '.') {
        (*p)++;
        if (!(**p >= '0' && **p <= '9')) return false;
        while (**p >= '0' && **p <= '9') (*p)++;
    }
    if (**p == 'e' || **p == 'E') {
        (*p)++;
        if (**p == '+' || **p == '-') (*p)++;
        if (!(**p >= '0' && **p <= '9')) return false;
        while (**p >= '0' && **p <= '9') (*p)++;
    }
    return true;
}

static bool tvj_value(const char **p);

static bool tvj_object(const char **p)
{
    (*p)++;                                    /* '{' */
    tvj_ws(p);
    if (**p == '}') { (*p)++; return true; }
    for (;;) {
        tvj_ws(p);
        if (!tvj_string(p)) return false;
        tvj_ws(p);
        if (**p != ':') return false;
        (*p)++;
        tvj_ws(p);
        if (!tvj_value(p)) return false;
        tvj_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; return true; }
        return false;
    }
}

static bool tvj_array(const char **p)
{
    (*p)++;                                    /* '[' */
    tvj_ws(p);
    if (**p == ']') { (*p)++; return true; }
    for (;;) {
        tvj_ws(p);
        if (!tvj_value(p)) return false;
        tvj_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == ']') { (*p)++; return true; }
        return false;
    }
}

static bool tvj_value(const char **p)
{
    switch (**p) {
    case '{': return tvj_object(p);
    case '[': return tvj_array(p);
    case '"': return tvj_string(p);
    case 't': if (strncmp(*p, "true", 4) == 0)  { *p += 4; return true; }  return false;
    case 'f': if (strncmp(*p, "false", 5) == 0) { *p += 5; return true; }  return false;
    case 'n': if (strncmp(*p, "null", 4) == 0)  { *p += 4; return true; }  return false;
    default:  return tvj_number(p);
    }
}

/* True when s is exactly one strict JSON document (plus whitespace). */
static bool tvj_valid(const char *s)
{
    if (s == NULL) return false;
    const char *p = s;
    tvj_ws(&p);
    if (!tvj_value(&p)) return false;
    tvj_ws(&p);
    return *p == '\0';
}

#endif /* TV_JSON_H */

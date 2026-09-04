/**
 * ndjson_parse.c - minimaler JSON-Leser, siehe ndjson_parse.h
 */

#include "ndjson_parse.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Scanner: liefert Zeiger hinter den Wert oder NULL bei Fehler        */

static const char*
skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static const char* skip_value(const char* p, const char* end);

static const char*
skip_string(const char* p, const char* end) {
    if (p >= end || *p != '"') {
        return NULL;
    }
    p++;
    while (p < end) {
        if (*p == '\\') {
            p += 2;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        p++;
    }
    return NULL;
}

static const char*
skip_number(const char* p, const char* end) {
    const char* start = p;
    if (p < end && (*p == '-' || *p == '+')) {
        p++;
    }
    while (p < end
           && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+'
               || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F') || *p == 'x' || *p == 'X')) {
        p++; /* tolerant: auch 0x.. akzeptieren (kein Standard-JSON) */
    }
    return (p > start) ? p : NULL;
}

static const char*
skip_container(const char* p, const char* end, char open, char close) {
    if (p >= end || *p != open) {
        return NULL;
    }
    p = skip_ws(p + 1, end);
    if (p < end && *p == close) {
        return p + 1;
    }
    for (;;) {
        if (open == '{') {
            p = skip_string(skip_ws(p, end), end); /* Key */
            if (p == NULL) {
                return NULL;
            }
            p = skip_ws(p, end);
            if (p >= end || *p != ':') {
                return NULL;
            }
            p++;
        }
        p = skip_value(skip_ws(p, end), end);
        if (p == NULL) {
            return NULL;
        }
        p = skip_ws(p, end);
        if (p < end && *p == ',') {
            p++;
            continue;
        }
        if (p < end && *p == close) {
            return p + 1;
        }
        return NULL;
    }
}

static const char*
skip_value(const char* p, const char* end) {
    if (p >= end) {
        return NULL;
    }
    switch (*p) {
        case '{': return skip_container(p, end, '{', '}');
        case '[': return skip_container(p, end, '[', ']');
        case '"': return skip_string(p, end);
        case 't': return (end - p >= 4 && memcmp(p, "true", 4) == 0) ? p + 4 : NULL;
        case 'f': return (end - p >= 5 && memcmp(p, "false", 5) == 0) ? p + 5 : NULL;
        case 'n': return (end - p >= 4 && memcmp(p, "null", 4) == 0) ? p + 4 : NULL;
        default: return skip_number(p, end);
    }
}

/* ------------------------------------------------------------------ */
/* API                                                                 */

bool
cj_parse(const char* s, size_t len, cj_t* out) {
    const char* end = s + len;
    const char* p = skip_ws(s, end);
    const char* q = skip_value(p, end);

    if (q == NULL || skip_ws(q, end) != end) {
        return false;
    }
    out->s = p;
    out->len = (size_t)(q - p);
    return true;
}

bool
cj_obj_get(cj_t obj, const char* key, cj_t* val) {
    const char* end = obj.s + obj.len;
    const char* p;
    size_t key_len = strlen(key);

    if (!cj_is_obj(obj)) {
        return false;
    }
    p = skip_ws(obj.s + 1, end);
    if (p < end && *p == '}') {
        return false;
    }
    for (;;) {
        const char* kstart = skip_ws(p, end);
        const char* kend = skip_string(kstart, end);

        if (kend == NULL) {
            return false;
        }
        p = skip_ws(kend, end);
        if (p >= end || *p != ':') {
            return false;
        }
        p = skip_ws(p + 1, end);

        const char* vend = skip_value(p, end);

        if (vend == NULL) {
            return false;
        }
        /* Schluessel vergleichen (ohne Unescaping - Keys sind ASCII) */
        if ((size_t)(kend - kstart) == key_len + 2 && memcmp(kstart + 1, key, key_len) == 0) {
            val->s = p;
            val->len = (size_t)(vend - p);
            return true;
        }
        p = skip_ws(vend, end);
        if (p < end && *p == ',') {
            p++;
            continue;
        }
        return false; /* '}' oder Fehler: Key nicht gefunden */
    }
}

bool
cj_arr_next(cj_t arr, size_t* iter, cj_t* val) {
    const char* end = arr.s + arr.len;
    const char* p;

    if (!cj_is_arr(arr)) {
        return false;
    }
    p = (*iter == 0) ? arr.s + 1 : arr.s + *iter;
    p = skip_ws(p, end);
    if (p < end && (*p == ']')) {
        return false;
    }
    if (*iter != 0) {
        if (p >= end || *p != ',') {
            return false;
        }
        p = skip_ws(p + 1, end);
    }

    const char* vend = skip_value(p, end);

    if (vend == NULL) {
        return false;
    }
    val->s = p;
    val->len = (size_t)(vend - p);
    *iter = (size_t)(vend - arr.s);
    return true;
}

bool
cj_as_i64(cj_t v, int64_t* out) {
    char buf[32];
    size_t n;

    if (v.len == 0) {
        return false;
    }
    if (cj_is_str(v)) {
        n = cj_as_str(v, buf, sizeof(buf));
        if (n == (size_t)-1 || n == 0) {
            return false;
        }
    } else {
        n = (v.len < sizeof(buf) - 1) ? v.len : sizeof(buf) - 1;
        memcpy(buf, v.s, n);
        buf[n] = '\0';
    }

    char* endp = NULL;
    long long val = strtoll(buf, &endp, 0); /* Basis 0: 123, 0x1F50, 0755 */

    if (endp == buf || (endp != NULL && *endp != '\0')) {
        return false;
    }
    *out = (int64_t)val;
    return true;
}

bool
cj_as_bool(cj_t v, bool* out) {
    if (v.len == 4 && memcmp(v.s, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (v.len == 5 && memcmp(v.s, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

size_t
cj_as_str(cj_t v, char* out, size_t out_size) {
    if (!cj_is_str(v) || v.len < 2 || out_size == 0) {
        return (size_t)-1;
    }

    const char* p = v.s + 1;
    const char* end = v.s + v.len - 1;
    size_t n = 0;

    while (p < end && n < out_size - 1) {
        char c = *p++;

        if (c == '\\' && p < end) {
            char e = *p++;

            switch (e) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    /* nur Latin-1-Teilmenge; Rest wird '?' */
                    unsigned int cp = 0;
                    if (end - p >= 4) {
                        char hex[5] = {p[0], p[1], p[2], p[3], 0};
                        cp = (unsigned int)strtoul(hex, NULL, 16);
                        p += 4;
                    }
                    c = (cp > 0 && cp < 256) ? (char)cp : '?';
                    break;
                }
                default: c = e; break; /* \" \\ \/ */
            }
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return n;
}

size_t
cj_quote(const char* s, size_t s_len, char* out, size_t out_size) {
    size_t n = 0;

    if (out_size < 3) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    out[n++] = '"';
    for (size_t i = 0; i < s_len && n + 3 < out_size; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c == '"' || c == '\\') {
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if (c == '\n') {
            out[n++] = '\\';
            out[n++] = 'n';
        } else if (c == '\r') {
            out[n++] = '\\';
            out[n++] = 'r';
        } else if (c == '\t') {
            out[n++] = '\\';
            out[n++] = 't';
        } else if (c < 0x20) {
            /* uebrige Steuerzeichen schlucken */
        } else {
            out[n++] = (char)c;
        }
    }
    out[n++] = '"';
    out[n] = '\0';
    return n;
}

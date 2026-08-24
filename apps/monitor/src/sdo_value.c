/**
 * sdo_value.c
 *
 * SDO-Rohdaten <-> Anzeigetext. Alle Werte auf dem Bus sind
 * little-endian (CiA 301); Ganzzahlen akzeptieren beim Schreiben
 * dezimale und 0x-Hex-Eingaben.
 */

#include "sdo_value.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char*
cb_dtype_name(cb_dtype_t dtype) {
    switch (dtype) {
        case CB_DT_BOOL: return "BOOL";
        case CB_DT_I8: return "I8";
        case CB_DT_I16: return "I16";
        case CB_DT_I32: return "I32";
        case CB_DT_U8: return "U8";
        case CB_DT_U16: return "U16";
        case CB_DT_U32: return "U32";
        case CB_DT_F32: return "F32";
        case CB_DT_STR: return "STR";
        case CB_DT_OCTET: return "OCTET";
        case CB_DT_I64: return "I64";
        case CB_DT_U64: return "U64";
        default: return "?";
    }
}

const char*
cb_access_name(cb_access_t access) {
    switch (access) {
        case CB_ACC_RO: return "ro";
        case CB_ACC_WO: return "wo";
        case CB_ACC_RW: return "rw";
        default: return "?";
    }
}

static uint64_t
load_le(const uint8_t* data, size_t len) {
    uint64_t v = 0;
    for (size_t i = 0; i < len && i < 8; i++) {
        v |= (uint64_t)data[i] << (8 * i);
    }
    return v;
}

static void
store_le(uint8_t* data, uint64_t v, size_t len) {
    for (size_t i = 0; i < len; i++) {
        data[i] = (uint8_t)(v >> (8 * i));
    }
}

static size_t
dtype_size(cb_dtype_t dtype) {
    switch (dtype) {
        case CB_DT_BOOL:
        case CB_DT_U8:
        case CB_DT_I8: return 1;
        case CB_DT_U16:
        case CB_DT_I16: return 2;
        case CB_DT_U32:
        case CB_DT_I32:
        case CB_DT_F32: return 4;
        case CB_DT_U64:
        case CB_DT_I64: return 8;
        default: return 0; /* STR/OCTET: variabel */
    }
}

static uint64_t
hex_mask(size_t nbytes) {
    if (nbytes >= 8) {
        return UINT64_MAX;
    }
    return (1ull << (nbytes * 8)) - 1ull;
}

static int
format_value(cb_dtype_t dtype, const uint8_t* data, size_t len, bool hex, char* out, size_t out_size) {
    if (out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    size_t want = dtype_size(dtype);
    if (want != 0 && len < want) {
        snprintf(out, out_size, "(%zu Bytes?)", len);
        return -1;
    }

    if (hex) {
        switch (dtype) {
            case CB_DT_BOOL:
            case CB_DT_U8:
            case CB_DT_I8:
            case CB_DT_U16:
            case CB_DT_I16:
            case CB_DT_U32:
            case CB_DT_I32:
            case CB_DT_U64:
            case CB_DT_I64: {
                uint64_t v = load_le(data, want) & hex_mask(want);
                static const char* fmts[] = {"0x%02" PRIX64, "0x%04" PRIX64, "0x%08" PRIX64, "0x%016" PRIX64};
                size_t k = want == 1 ? 0 : want == 2 ? 1 : want == 4 ? 2 : 3;
                snprintf(out, out_size, fmts[k], v);
                return 0;
            }
            default:
                break;
        }
    }

    switch (dtype) {
        case CB_DT_BOOL:
            snprintf(out, out_size, "%s", data[0] ? "1" : "0");
            break;
        case CB_DT_U8:
        case CB_DT_U16:
        case CB_DT_U32:
        case CB_DT_U64:
            snprintf(out, out_size, "%" PRIu64, load_le(data, want));
            break;
        case CB_DT_I8:
            snprintf(out, out_size, "%" PRId8, (int8_t)data[0]);
            break;
        case CB_DT_I16:
            snprintf(out, out_size, "%" PRId16, (int16_t)load_le(data, 2));
            break;
        case CB_DT_I32:
            snprintf(out, out_size, "%" PRId32, (int32_t)load_le(data, 4));
            break;
        case CB_DT_I64:
            snprintf(out, out_size, "%" PRId64, (int64_t)load_le(data, 8));
            break;
        case CB_DT_F32: {
            uint32_t raw = (uint32_t)load_le(data, 4);
            float f;
            memcpy(&f, &raw, sizeof(f));
            snprintf(out, out_size, "%g", (double)f);
            break;
        }
        case CB_DT_STR: {
            size_t n = 0;
            for (; n < len && n + 1 < out_size; n++) {
                char c = (char)data[n];
                if (c == '\0') {
                    break;
                }
                out[n] = isprint((unsigned char)c) ? c : '.';
            }
            out[n] = '\0';
            break;
        }
        case CB_DT_OCTET: {
            size_t pos = 0;
            for (size_t i = 0; i < len && pos + 3 < out_size; i++) {
                pos += (size_t)snprintf(out + pos, out_size - pos, "%02X", data[i]);
            }
            break;
        }
        default:
            return -1;
    }
    return 0;
}

int
cb_value_format(cb_dtype_t dtype, const uint8_t* data, size_t len, char* out, size_t out_size) {
    return format_value(dtype, data, len, false, out, out_size);
}

int
cb_value_display(cb_dtype_t dtype, const char* text, bool hex, char* out, size_t out_size) {
    if (text == NULL || text[0] == '\0' || text[0] == '<' || out_size == 0) {
        return -1;
    }
    switch (dtype) {
        case CB_DT_BOOL:
        case CB_DT_U8:
        case CB_DT_I8:
        case CB_DT_U16:
        case CB_DT_I16:
        case CB_DT_U32:
        case CB_DT_I32:
        case CB_DT_U64:
        case CB_DT_I64:
            break;
        default:
            return -1;
    }

    uint8_t buf[CB_DP_DATA_MAX];
    size_t len = 0;
    if (cb_value_parse(dtype, text, buf, sizeof(buf), &len) != 0) {
        return -1;
    }
    return format_value(dtype, buf, len, hex, out, out_size);
}

static int
parse_i64(const char* text, int64_t min, int64_t max, int64_t* value) {
    char* end = NULL;
    errno = 0;
    long long v = strtoll(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || v < min || v > max) {
        return -1;
    }
    *value = v;
    return 0;
}

static int
parse_u64(const char* text, uint64_t max, uint64_t* value) {
    char* end = NULL;
    if (text[0] == '-') {
        return -1;
    }
    errno = 0;
    unsigned long long v = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || v > max) {
        return -1;
    }
    *value = v;
    return 0;
}

int
cb_value_parse(cb_dtype_t dtype, const char* text, uint8_t* data, size_t data_size, size_t* len) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if (*text == '\0' && dtype != CB_DT_STR) {
        return -1;
    }

    size_t want = dtype_size(dtype);
    if (want > data_size) {
        return -1;
    }

    switch (dtype) {
        case CB_DT_BOOL: {
            uint64_t v;
            if (strcmp(text, "true") == 0) {
                v = 1;
            } else if (strcmp(text, "false") == 0) {
                v = 0;
            } else if (parse_u64(text, 1, &v) != 0) {
                return -1;
            }
            data[0] = (uint8_t)v;
            break;
        }
        case CB_DT_U8:
        case CB_DT_U16:
        case CB_DT_U32:
        case CB_DT_U64: {
            static const uint64_t umax[] = {UINT8_MAX, UINT16_MAX, UINT32_MAX, UINT64_MAX};
            uint64_t max = umax[want == 1 ? 0 : want == 2 ? 1 : want == 4 ? 2 : 3];
            uint64_t v;
            if (parse_u64(text, max, &v) != 0) {
                return -1;
            }
            store_le(data, v, want);
            break;
        }
        case CB_DT_I8:
        case CB_DT_I16:
        case CB_DT_I32:
        case CB_DT_I64: {
            static const int64_t imin[] = {INT8_MIN, INT16_MIN, INT32_MIN, INT64_MIN};
            static const int64_t imax[] = {INT8_MAX, INT16_MAX, INT32_MAX, INT64_MAX};
            size_t k = want == 1 ? 0 : want == 2 ? 1 : want == 4 ? 2 : 3;
            int64_t v;
            if (parse_i64(text, imin[k], imax[k], &v) != 0) {
                return -1;
            }
            store_le(data, (uint64_t)v, want);
            break;
        }
        case CB_DT_F32: {
            char* end = NULL;
            errno = 0;
            float f = strtof(text, &end);
            if (errno != 0 || end == text || *end != '\0') {
                return -1;
            }
            uint32_t raw;
            memcpy(&raw, &f, sizeof(raw));
            store_le(data, raw, 4);
            break;
        }
        case CB_DT_STR: {
            size_t n = strlen(text);
            if (n > data_size) {
                n = data_size;
            }
            memcpy(data, text, n);
            *len = n;
            return 0;
        }
        case CB_DT_OCTET: {
            size_t n = 0;
            size_t textlen = strlen(text);
            if (textlen == 0 || textlen % 2 != 0) {
                return -1;
            }
            for (size_t i = 0; i + 1 < textlen && n < data_size; i += 2) {
                char hex[3] = {text[i], text[i + 1], '\0'};
                char* end = NULL;
                long v = strtol(hex, &end, 16);
                if (*end != '\0') {
                    return -1;
                }
                data[n++] = (uint8_t)v;
            }
            *len = n;
            return 0;
        }
        default:
            return -1;
    }

    *len = want;
    return 0;
}

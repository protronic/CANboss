/**
 * test_values.c
 *
 * Selbsttest der Wertkonvertierung (Pendant zu den cargo-Tests des
 * Rust-Originals). Bauen und ausfuehren mit: make test
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "canboss.h"
#include "sdo_value.h"

static void
roundtrip(cb_dtype_t dtype, const char* text, size_t expect_len) {
    uint8_t buf[CB_DP_DATA_MAX];
    char out[CB_VALUE_MAX];
    size_t len = 0;

    assert(cb_value_parse(dtype, text, buf, sizeof(buf), &len) == 0);
    assert(len == expect_len);
    assert(cb_value_format(dtype, buf, len, out, sizeof(out)) == 0);
    if (strcmp(out, text) != 0) {
        fprintf(stderr, "Roundtrip %d: '%s' -> '%s'\n", (int)dtype, text, out);
        assert(0);
    }
}

int
main(void) {
    uint8_t buf[CB_DP_DATA_MAX];
    char out[CB_VALUE_MAX];
    size_t len = 0;

    /* Ganzzahlen, wie sie die gtwa-Tests des Originals nutzten */
    roundtrip(CB_DT_U8, "255", 1);
    roundtrip(CB_DT_U16, "65535", 2);
    roundtrip(CB_DT_U32, "4294967295", 4);
    roundtrip(CB_DT_I8, "-128", 1);
    roundtrip(CB_DT_I16, "-1000", 2);
    roundtrip(CB_DT_I32, "2147483647", 4);
    roundtrip(CB_DT_BOOL, "1", 1);
    roundtrip(CB_DT_U64, "18446744073709551615", 8);
    roundtrip(CB_DT_I64, "-9223372036854775808", 8);

    /* Hex-Eingabe */
    assert(cb_value_parse(CB_DT_U16, "0x2650", buf, sizeof(buf), &len) == 0);
    assert(len == 2 && buf[0] == 0x50 && buf[1] == 0x26);

    /* Little-endian Ablage */
    assert(cb_value_parse(CB_DT_U32, "305419896", buf, sizeof(buf), &len) == 0); /* 0x12345678 */
    assert(buf[0] == 0x78 && buf[1] == 0x56 && buf[2] == 0x34 && buf[3] == 0x12);

    /* Float */
    assert(cb_value_parse(CB_DT_F32, "1.5", buf, sizeof(buf), &len) == 0);
    assert(len == 4);
    assert(cb_value_format(CB_DT_F32, buf, len, out, sizeof(out)) == 0);
    assert(strcmp(out, "1.5") == 0);

    /* Strings (VISIBLE_STRING ohne NUL auf dem Bus) */
    assert(cb_value_parse(CB_DT_STR, "Demo-Sensor", buf, sizeof(buf), &len) == 0);
    assert(len == 11);
    assert(cb_value_format(CB_DT_STR, buf, len, out, sizeof(out)) == 0);
    assert(strcmp(out, "Demo-Sensor") == 0);

    /* OCTET_STRING als Hex */
    assert(cb_value_parse(CB_DT_OCTET, "DEADBEEF", buf, sizeof(buf), &len) == 0);
    assert(len == 4 && buf[0] == 0xDE && buf[3] == 0xEF);
    assert(cb_value_format(CB_DT_OCTET, buf, len, out, sizeof(out)) == 0);
    assert(strcmp(out, "DEADBEEF") == 0);

    /* Anzeige Dezimal <-> Hex (Taste H) */
    assert(cb_value_display(CB_DT_U8, "255", true, out, sizeof(out)) == 0);
    assert(strcmp(out, "0xFF") == 0);
    assert(cb_value_display(CB_DT_U8, "0xFF", false, out, sizeof(out)) == 0);
    assert(strcmp(out, "255") == 0);
    assert(cb_value_display(CB_DT_U32, "0x00000194", false, out, sizeof(out)) == 0);
    assert(strcmp(out, "404") == 0);
    assert(cb_value_display(CB_DT_U32, "404", true, out, sizeof(out)) == 0);
    assert(strcmp(out, "0x00000194") == 0);
    assert(cb_value_display(CB_DT_I8, "-1", true, out, sizeof(out)) == 0);
    assert(strcmp(out, "0xFF") == 0);
    assert(cb_value_display(CB_DT_BOOL, "1", true, out, sizeof(out)) == 0);
    assert(strcmp(out, "0x01") == 0);
    assert(cb_value_display(CB_DT_F32, "1.5", true, out, sizeof(out)) != 0);
    assert(cb_value_display(CB_DT_STR, "Demo", true, out, sizeof(out)) != 0);
    assert(cb_value_display(CB_DT_U16, "<timeout>", true, out, sizeof(out)) != 0);

    /* Fehlerfaelle */
    assert(cb_value_parse(CB_DT_U8, "256", buf, sizeof(buf), &len) != 0);
    assert(cb_value_parse(CB_DT_U8, "-1", buf, sizeof(buf), &len) != 0);
    assert(cb_value_parse(CB_DT_I8, "abc", buf, sizeof(buf), &len) != 0);
    assert(cb_value_parse(CB_DT_OCTET, "XYZ", buf, sizeof(buf), &len) != 0);

    /* Registry aus dem Generator ist eingebunden */
    assert(cb_net_node_count > 0);
    for (uint16_t i = 0; i < cb_net_node_count; i++) {
        assert(cb_net_nodes[i]->dp_count > 0);
        assert(cb_net_nodes[i]->obj_count > 0);
        /* Objektbereiche muessen lueckenlos auf dps[] zeigen */
        uint16_t pos = 0;
        for (uint16_t o = 0; o < cb_net_nodes[i]->obj_count; o++) {
            assert(cb_net_nodes[i]->objs[o].dp_first == pos);
            pos += cb_net_nodes[i]->objs[o].dp_count;
        }
        assert(pos == cb_net_nodes[i]->dp_count);
    }

    printf("test_values: alle Tests bestanden (%u Knoten in der Registry)\n", cb_net_node_count);
    return 0;
}

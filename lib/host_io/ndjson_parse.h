/**
 * ndjson_parse.h
 *
 * Minimaler JSON-Leser fuer das ndjson-Protokoll (lib/host_io):
 * arbeitet zerstoerungsfrei auf Text-Spannen (kein DOM, keine
 * Allokation). Unterstuetzt Objekte, Arrays, Strings (inkl. \uXXXX),
 * Zahlen, true/false/null - genug fuer die Kommandos der Webapp.
 *
 * Kein Ersatz fuer einen validierenden Parser: fehlerhafte Eingaben
 * werden erkannt und abgelehnt, aber Fehlerpositionen o.ae. gibt es
 * nicht.
 */

#ifndef CB_NDJSON_PARSE_H_
#define CB_NDJSON_PARSE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rohtext-Ausschnitt eines JSON-Werts (inkl. Anfuehrungszeichen bei
 * Strings, inkl. Klammern bei Objekt/Array). */
typedef struct {
    const char* s;
    size_t len;
} cj_t;

/* Text als JSON-Wert fassen (fuehrende/folgende Leerzeichen erlaubt).
 * Rueckgabe false, wenn kein einzelner gueltiger Wert. */
bool cj_parse(const char* s, size_t len, cj_t* out);

static inline bool
cj_is_obj(cj_t v) {
    return v.len > 0 && v.s[0] == '{';
}

static inline bool
cj_is_arr(cj_t v) {
    return v.len > 0 && v.s[0] == '[';
}

static inline bool
cj_is_str(cj_t v) {
    return v.len > 0 && v.s[0] == '"';
}

/* Wert zu key in einem Objekt suchen. */
bool cj_obj_get(cj_t obj, const char* key, cj_t* val);

/* Array iterieren: *iter mit 0 starten, liefert false am Ende. */
bool cj_arr_next(cj_t arr, size_t* iter, cj_t* val);

/* Zahl (Integer) oder String wie "0x1F50"/"123" nach int64. */
bool cj_as_i64(cj_t v, int64_t* out);

bool cj_as_bool(cj_t v, bool* out);

/* String entkommen (unescape) nach out (NUL-terminiert). Rueckgabe:
 * Nutzlaenge, oder (size_t)-1 wenn v kein String ist/nicht passt. */
size_t cj_as_str(cj_t v, char* out, size_t out_size);

/* JSON-String-Escaping fuer Ausgaben: schreibt s als "..."-Literal
 * (mit Anfuehrungszeichen) nach out. Rueckgabe: geschriebene Bytes,
 * bei Platzmangel wird gekuerzt (immer gueltig terminiertes Literal). */
size_t cj_quote(const char* s, size_t s_len, char* out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* CB_NDJSON_PARSE_H_ */

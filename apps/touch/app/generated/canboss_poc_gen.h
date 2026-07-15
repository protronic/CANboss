/* @generated von tools/poc2lvgl.py - nicht editieren */
#ifndef CANBOSS_POC_GEN_H_
#define CANBOSS_POC_GEN_H_

#include <stdint.h>
#include <stdbool.h>

#define CANBOSS_POC_FIELD_COUNT   4u
#define CANBOSS_POC_BUTTON_COUNT  15u
#define CANBOSS_POC_GROUP_BASE    12u

#define CANBOSS_POC_CAN_ENABLED   1
#define CANBOSS_POC_TX_ID         0x200u
#define CANBOSS_POC_TX_LITTLE     1
#define CANBOSS_POC_MINP_ID       0x285u
#define CANBOSS_POC_REPEAT_MS     25u
#define CANBOSS_POC_REFRESH_MS    1000u /* Idle-Keepalive (Release-Payload) */

typedef struct {
    const char* eyebrow;
    const char* title;
} canboss_poc_field_t;

typedef struct {
    uint8_t active; /* 0 = kein Feedback-Bit fuer diesen Button */
    uint8_t byte_index;
    uint8_t bit_index;
} canboss_poc_minp_t;

extern const char* const canboss_poc_hall_name;
extern const char* const canboss_poc_page_title;
extern const canboss_poc_field_t canboss_poc_fields[CANBOSS_POC_FIELD_COUNT];
/* Beschriftungen der 3 Buttons je Spalte (Feld i -> Button-Index i*3+j;
 * letzte Spalte = Zentral-Gruppe ab CANBOSS_POC_GROUP_BASE) */
extern const char* const canboss_poc_button_labels[CANBOSS_POC_BUTTON_COUNT];
extern const canboss_poc_minp_t canboss_poc_minp[CANBOSS_POC_BUTTON_COUNT];

#endif /* CANBOSS_POC_GEN_H_ */

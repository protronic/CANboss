/**
 * canboss_poc_can_stm32.c
 *
 * Target-CAN-Backend der PoC-Hallenlichtsteuerung: Raw-11-Bit-Frames ueber
 * denselben FDCAN wie der CANopen-Stack.
 *
 *  - TX ueber die HAL-TX-FIFO, per CO_LOCK_CAN_SEND gegen gleichzeitige
 *    CANopenNode-Sendungen serialisiert (gleiche Sperre wie CO_CANsend)
 *  - RX ueber CO_CANrawRxHook() im STM32-Port (CO_driver_STM32.c):
 *    Frames, die kein CANopen-RX-Puffer beansprucht, landen hier
 *    (ISR-Kontext) und werden an canboss_poc_can_rx() durchgereicht
 *
 * Host-Gegenstueck: host/canboss_poc_can_host.c (SocketCAN).
 */

#include "canboss_poc.h"
#include "canboss_poc_gen.h"

#include "main.h"
#include "fdcan.h"
#include "CANopen.h"
#include "CO_app_STM32.h"

bool
canboss_poc_can_tx(const uint8_t payload[6]) {
    if (canopenNodeSTM32 == NULL || canopenNodeSTM32->canOpenStack == NULL) {
        return false;
    }

    CO_CANmodule_t* mod = canopenNodeSTM32->canOpenStack->CANmodule;
    FDCAN_HandleTypeDef* h = canopenNodeSTM32->CANHandle;
    FDCAN_TxHeaderTypeDef tx_hdr;
    bool ok = false;

    tx_hdr.Identifier = CANBOSS_POC_TX_ID;
    tx_hdr.IdType = FDCAN_STANDARD_ID;
    tx_hdr.TxFrameType = FDCAN_DATA_FRAME;
    tx_hdr.DataLength = FDCAN_DLC_BYTES_6;
    tx_hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_hdr.BitRateSwitch = FDCAN_BRS_OFF;
    tx_hdr.FDFormat = FDCAN_CLASSIC_CAN;
    tx_hdr.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_hdr.MessageMarker = 0;

    CO_LOCK_CAN_SEND(mod);
    if (HAL_FDCAN_GetTxFifoFreeLevel(h) > 0) {
        ok = (HAL_FDCAN_AddMessageToTxFifoQ(h, &tx_hdr, (uint8_t*)payload) == HAL_OK);
    }
    CO_UNLOCK_CAN_SEND(mod);
    return ok;
}

/* Unmatched-Frame-Hook des CANopenNode-STM32-Ports (ueberschreibt den
 * schwachen Default in CO_driver_STM32.c; laeuft im FDCAN-ISR-Kontext). */
void
CO_CANrawRxHook(uint16_t ident, const uint8_t* data, uint8_t dlc) {
    canboss_poc_can_rx(ident, data, dlc);
}

# USB-Demo (STM32H573I-DK)

Minimal-Firmware zum USB-Bring-up: **Zephyr-Shell + Berry-GPIO**, ohne
CAN, Gateway oder CANopen.

| Stecker | Funktion |
|---|---|
| **CN10 ST-LINK** | Flash, SWD, VCP @ 115200. Immer `/dev/ttyACM0` (VID `0483:374e`). Prompt `uart:~$` |
| **CN17 User USB-C** | MCU USB_DRD_FS. Soll als `1209:0001` enumerieren, zweite ACM, Prompt `usb:~$` |

JP4 auf STLK [1-2], JP6 bestueckt. Das CAN-Shield bleibt ab.

## Bauen / Flash

```bash
west build -p always -b stm32h573i_dk CANboss/apps/usbdemo -d build-usbdemo-h573
export PATH=/opt/stm32cubeprog/bin:$PATH
west flash --cli=/opt/stm32cubeprog/bin/STM32_Programmer_CLI -d build-usbdemo-h573
```

Konsole:

```bash
picocom -b 115200 /dev/ttyACM0
```

Erwartete Logs: `UCPD Dead-Battery Rd an`, `TCPP: NORMAL ... VBUS_OK`,
`USB-Device aktiv ... DPPU=1`. Danach `lsusb -d 1209:0001`. Wenn das
Device kommt, zweite ACM oeffnen (`usb:~$`).

## Berry

Auf jeder Shell:

```
uart:~$ berry help()
uart:~$ berry led(0, true)
uart:~$ berry
berry> btn()
berry> pin("I", 9, 0)
berry> exit
```

`led(0..3)` = LD1 gruen / LD2 orange / LD3 rot / LD4 blau (ACTIVE_LOW).
`btn()` = User-Taste PC13. `pin("A".."I", n [, v])` = beliebiger GPIO.
Nicht anfassen: PA11/PA12 (USB), PG0 (TCPP), PB8/PB9 (I2C4).

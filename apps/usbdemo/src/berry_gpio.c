/**
 * Berry-VM fuer das USB-Demo: GPIO der STM32H573I-DK (LEDs, User-Taste,
 * beliebige Ports). Kein CANopen, keine OD-Bindings.
 *
 *   led(0..3 [, on])     LD1 gruen PI9, LD2 orange PI8, LD3 rot PF1, LD4 blau PF4
 *   btn()                User-Taste PC13, true = gedrueckt
 *   pin("I", 9 [, v])    beliebiger Pin lesen/schreiben (Pegel 0/1)
 *   help()
 */

#include "berry_gpio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "berry.h"

static bvm* g_vm;
static struct k_mutex g_vm_lock;

static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET(DT_NODELABEL(green_led_0), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(orange_led_0), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(red_led_0), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led_0), gpios),
};

static const struct gpio_dt_spec user_btn = GPIO_DT_SPEC_GET(DT_NODELABEL(user_button), gpios);

static const struct device*
port_dev(char letter) {
    switch (letter) {
    case 'A':
    case 'a':
        return DEVICE_DT_GET(DT_NODELABEL(gpioa));
    case 'B':
    case 'b':
        return DEVICE_DT_GET(DT_NODELABEL(gpiob));
    case 'C':
    case 'c':
        return DEVICE_DT_GET(DT_NODELABEL(gpioc));
    case 'D':
    case 'd':
        return DEVICE_DT_GET(DT_NODELABEL(gpiod));
    case 'E':
    case 'e':
        return DEVICE_DT_GET(DT_NODELABEL(gpioe));
    case 'F':
    case 'f':
        return DEVICE_DT_GET(DT_NODELABEL(gpiof));
    case 'G':
    case 'g':
        return DEVICE_DT_GET(DT_NODELABEL(gpiog));
    case 'H':
    case 'h':
        return DEVICE_DT_GET(DT_NODELABEL(gpioh));
    case 'I':
    case 'i':
        return DEVICE_DT_GET(DT_NODELABEL(gpioi));
    default:
        return NULL;
    }
}

static int
raise_type(bvm* vm, const char* msg) {
    be_raise(vm, "type_error", msg);
    be_return_nil(vm);
}

static int
m_led(bvm* vm) {
    int argc = be_top(vm);

    if (argc < 1 || !be_isint(vm, 1)) {
        return raise_type(vm, "led(n [, on])  n=0..3");
    }

    int n = (int)be_toint(vm, 1);
    if (n < 0 || n >= (int)ARRAY_SIZE(leds)) {
        return raise_type(vm, "led: n muss 0..3 sein (LD1..LD4)");
    }

    if (!gpio_is_ready_dt(&leds[n])) {
        return raise_type(vm, "led: GPIO nicht bereit");
    }

    (void)gpio_pin_configure_dt(&leds[n], GPIO_OUTPUT_INACTIVE);

    if (argc >= 2) {
        bool on = be_isbool(vm, 2) ? (bool)be_tobool(vm, 2) : (be_toint(vm, 2) != 0);
        (void)gpio_pin_set_dt(&leds[n], on ? 1 : 0);
        be_pushbool(vm, on);
        be_return(vm);
    }

    int v = gpio_pin_get_dt(&leds[n]);
    be_pushbool(vm, v > 0);
    be_return(vm);
}

static int
m_btn(bvm* vm) {
    if (!gpio_is_ready_dt(&user_btn)) {
        return raise_type(vm, "btn: GPIO nicht bereit");
    }

    (void)gpio_pin_configure_dt(&user_btn, GPIO_INPUT);
    be_pushbool(vm, gpio_pin_get_dt(&user_btn) > 0);
    be_return(vm);
}

static int
m_pin(bvm* vm) {
    int argc = be_top(vm);

    if (argc < 2 || !be_isstring(vm, 1) || !be_isint(vm, 2)) {
        return raise_type(vm, "pin(\"A\"..\"I\", n [, v])");
    }

    const char* port = be_tostring(vm, 1);
    if (port[0] == '\0' || port[1] != '\0') {
        return raise_type(vm, "pin: Port ist ein Buchstabe A..I");
    }

    int pin = (int)be_toint(vm, 2);
    if (pin < 0 || pin > 15) {
        return raise_type(vm, "pin: n muss 0..15 sein");
    }

    const struct device* dev = port_dev(port[0]);
    if (dev == NULL || !device_is_ready(dev)) {
        return raise_type(vm, "pin: Port unbekannt oder nicht bereit");
    }

    if (argc >= 3) {
        int v = be_isbool(vm, 3) ? (be_tobool(vm, 3) ? 1 : 0) : (int)be_toint(vm, 3);
        if (gpio_pin_configure(dev, (gpio_pin_t)pin, GPIO_OUTPUT) != 0) {
            return raise_type(vm, "pin: Configure als Ausgang fehlgeschlagen");
        }
        (void)gpio_pin_set(dev, (gpio_pin_t)pin, v ? 1 : 0);
        be_pushint(vm, v ? 1 : 0);
        be_return(vm);
    }

    if (gpio_pin_configure(dev, (gpio_pin_t)pin, GPIO_INPUT) != 0) {
        return raise_type(vm, "pin: Configure als Eingang fehlgeschlagen");
    }
    be_pushint(vm, gpio_pin_get(dev, (gpio_pin_t)pin) > 0 ? 1 : 0);
    be_return(vm);
}

static int
m_help(bvm* vm) {
    static const char help[] =
        "Berry " BERRY_VERSION " auf STM32H573I-DK (USB-Demo, kein CAN)\n"
        "\n"
        "  led(0..3 [, on])     LD1 gruen, LD2 orange, LD3 rot, LD4 blau\n"
        "  btn()                User-Taste, true = gedrueckt\n"
        "  pin(\"I\", 9 [, v])    Port A..I, Pin 0..15 lesen/schreiben\n"
        "  help()\n"
        "\n"
        "Beispiele:\n"
        "  led(0, true)         LD1 an\n"
        "  led(3, false)        LD4 aus\n"
        "  print(btn())         Taste?\n"
        "  pin(\"I\", 9, 0)       PI9 low (LD1-Anode, ACTIVE_LOW = LED an)\n"
        "\n"
        "Nicht anfassen: PA11/PA12 (USB), PG0 (TCPP), PB8/PB9 (I2C4).\n"
        "REPL: 'berry' ohne Argumente, Ende mit exit / quit / Strg-D.\n";

    be_writebuffer(help, strlen(help));
    be_return_nil(vm);
}

int
usbdemo_berry_exec(const char* code) {
    if (g_vm == NULL || code == NULL) {
        return -1;
    }

    k_mutex_lock(&g_vm_lock, K_FOREVER);

    int ret;
    size_t n = strlen(code) + 16;
    char* expr = malloc(n);
    if (expr != NULL) {
        snprintf(expr, n, "return (%s)", code);
        ret = be_loadstring(g_vm, expr);
        free(expr);
        if (ret != 0) {
            be_pop(g_vm, 1);
            ret = be_loadstring(g_vm, code);
        }
    } else {
        ret = be_loadstring(g_vm, code);
    }

    if (ret == 0) {
        ret = be_pcall(g_vm, 0);
    }

    if (ret != 0) {
        be_dumpexcept(g_vm);
    } else {
        if (!be_isnil(g_vm, -1)) {
            const char* s = be_tostring(g_vm, -1);
            be_writebuffer(s, strlen(s));
            be_writebuffer("\n", 1);
        }
        be_pop(g_vm, 1);
    }

    k_mutex_unlock(&g_vm_lock);
    return ret == 0 ? 0 : -1;
}

void
usbdemo_berry_init(void) {
    if (g_vm != NULL) {
        return;
    }

    k_mutex_init(&g_vm_lock);
    g_vm = be_vm_new();
    be_regfunc(g_vm, "led", m_led);
    be_regfunc(g_vm, "btn", m_btn);
    be_regfunc(g_vm, "pin", m_pin);
    be_regfunc(g_vm, "help", m_help);
}

static void
shell_sink(void* user, const char* buf, size_t len) {
    const struct shell* sh = user;
    shell_fprintf(sh, SHELL_NORMAL, "%.*s", (int)len, buf);
}

#define REPL_LINE_MAX 256

struct berry_repl {
    const struct shell* sh;
    char line[REPL_LINE_MAX];
    size_t pos;
};

static struct berry_repl g_repl;

static void
repl_prompt(const struct shell* sh) {
    shell_fprintf(sh, SHELL_NORMAL, "berry> ");
}

static void
repl_stop(const struct shell* sh) {
    shell_set_bypass(sh, NULL, NULL);
    usbdemo_berry_set_sink(NULL, NULL);
    shell_print(sh, "Berry-REPL beendet");
}

static void
repl_run_line(struct berry_repl* r) {
    r->line[r->pos] = '\0';
    r->pos = 0;

    if (r->line[0] == '\0') {
        repl_prompt(r->sh);
        return;
    }
    if (strcmp(r->line, "exit") == 0 || strcmp(r->line, "quit") == 0) {
        repl_stop(r->sh);
        return;
    }

    usbdemo_berry_set_sink(shell_sink, (void*)r->sh);
    (void)usbdemo_berry_exec(r->line);
    repl_prompt(r->sh);
}

static void
repl_bypass(const struct shell* sh, uint8_t* data, size_t len, void* user) {
    struct berry_repl* r = user;

    ARG_UNUSED(sh);
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        if (c == 0x04 && r->pos == 0) {
            repl_stop(r->sh);
            return;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            shell_fprintf(r->sh, SHELL_NORMAL, "\n");
            repl_run_line(r);
            continue;
        }
        if (c == '\b' || c == 0x7f) {
            if (r->pos > 0) {
                r->pos--;
                shell_fprintf(r->sh, SHELL_NORMAL, "\b \b");
            }
            continue;
        }
        if (c < 32 || r->pos + 1 >= sizeof(r->line)) {
            continue;
        }
        r->line[r->pos++] = (char)c;
        shell_fprintf(r->sh, SHELL_NORMAL, "%c", c);
    }
}

static int
cmd_berry(const struct shell* sh, size_t argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "repl") == 0) {
        g_repl.sh = sh;
        g_repl.pos = 0;
        usbdemo_berry_set_sink(shell_sink, (void*)sh);
        shell_print(sh, "Berry-REPL (exit / quit / Strg-D). help() zeigt GPIO-API.");
        repl_prompt(sh);
        shell_set_bypass(sh, repl_bypass, &g_repl);
        return 0;
    }

    static char code[CONFIG_SHELL_CMD_BUFF_SIZE];
    code[0] = '\0';
    size_t pos = 0;
    for (size_t i = 1; i < argc; i++) {
        int w = snprintf(code + pos, sizeof(code) - pos, "%s%s", i > 1 ? " " : "", argv[i]);
        if (w < 0 || (size_t)w >= sizeof(code) - pos) {
            break;
        }
        pos += (size_t)w;
    }

    usbdemo_berry_set_sink(shell_sink, (void*)sh);
    int ret = usbdemo_berry_exec(code);
    usbdemo_berry_set_sink(NULL, NULL);
    return ret;
}

SHELL_CMD_ARG_REGISTER(berry, NULL,
                       "Berry: 'berry' startet REPL, 'berry led(0, true)' Einzeiler",
                       cmd_berry, 1, 32);

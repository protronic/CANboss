#pragma once

#include <stddef.h>

void usbdemo_berry_init(void);
int usbdemo_berry_exec(const char* code);

typedef void (*usbdemo_berry_sink_t)(void* user, const char* buf, size_t len);

void usbdemo_berry_set_sink(usbdemo_berry_sink_t sink, void* user);

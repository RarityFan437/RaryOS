#pragma once
#include <stdint.h>

void usb_init(void);

uint64_t usb_get_isr_count(void);
uint64_t usb_get_keys_count(void);
uint64_t usb_get_push_count(void);
uint64_t usb_get_bad_code_count(void);

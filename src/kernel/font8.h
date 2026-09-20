#ifndef FONT8_H
#define FONT8_H

#include <stdint.h>

#define FONT_H 32 // Новая высота шрифта

extern const uint32_t font_data[95][FONT_H]; // Изменили тип на uint32_t
extern const uint8_t font_widths[95];

#endif

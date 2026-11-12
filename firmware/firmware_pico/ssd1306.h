#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

// OLED 0.91" 128x32, I2C, контроллер SSD1306
#define SSD1306_ADDR       0x3C
#define SSD1306_WIDTH      128
#define SSD1306_HEIGHT     32
#define SSD1306_PAGES      (SSD1306_HEIGHT / 8)   // 4 страницы

bool ssd1306_init(i2c_inst_t *i2c);
void ssd1306_clear(void);
void ssd1306_update(void);                          // сброс всего буфера на дисплей
void ssd1306_draw_string(uint8_t x, uint8_t y, const char *str);
void ssd1306_draw_char(uint8_t x, uint8_t y, char c);
void ssd1306_invert(bool invert);

#endif

#include "ssd1306.h"
#include "font.h"
#include <string.h>
#include "pico/stdlib.h"

static i2c_inst_t *g_i2c = NULL;
static uint8_t fb[SSD1306_WIDTH * SSD1306_PAGES];  // 128 * 4 = 512 байт

static void cmd(uint8_t c) {
    uint8_t buf[2] = {0x00, c};
    i2c_write_blocking(g_i2c, SSD1306_ADDR, buf, 2, false);
}

static void write_page(uint8_t page) {
    cmd(0xB0 + page);              // page start address
    cmd(0x00);                     // low column = 0
    cmd(0x10);                     // high column = 0
    uint8_t buf[SSD1306_WIDTH + 1];
    buf[0] = 0x40;                 // data
    memcpy(buf + 1, &fb[page * SSD1306_WIDTH], SSD1306_WIDTH);
    i2c_write_blocking(g_i2c, SSD1306_ADDR, buf, SSD1306_WIDTH + 1, false);
}

bool ssd1306_init(i2c_inst_t *i2c) {
    g_i2c = i2c;
    sleep_ms(50);
    cmd(0xAE);              // display off
    cmd(0xD5); cmd(0x80);   // clock divide
    cmd(0xA8); cmd(0x1F);   // multiplex = 32
    cmd(0xD3); cmd(0x00);   // display offset
    cmd(0x40);              // start line
    cmd(0x8D); cmd(0x14);   // charge pump on
    cmd(0x20); cmd(0x00);   // memory mode: horizontal
    cmd(0xA1);              // segment remap
    cmd(0xC8);              // COM scan down
    cmd(0xDA); cmd(0x02);   // COM pins
    cmd(0x81); cmd(0xCF);   // contrast
    cmd(0xD9); cmd(0xF1);   // precharge
    cmd(0xDB); cmd(0x40);   // VCOMH deselect
    cmd(0xA4);              // resume from RAM
    cmd(0xA6);              // normal (not inverted)
    cmd(0xAF);              // display on
    ssd1306_clear();
    ssd1306_update();
    return true;
}

void ssd1306_clear(void) {
    memset(fb, 0, sizeof(fb));
}

void ssd1306_update(void) {
    for (uint8_t p = 0; p < SSD1306_PAGES; p++)
        write_page(p);
}

void ssd1306_draw_char(uint8_t x, uint8_t y, char c) {
    if (c < 0x20 || c > 0x7F) c = 0x20;
    const uint8_t *g = font5x7[c - 0x20];
    uint8_t page = y >> 3;
    uint8_t bit  = y & 7;
    for (uint8_t i = 0; i < 5; i++) {
        if (x + i >= SSD1306_WIDTH) break;
        uint8_t col = g[i];
        if (page < SSD1306_PAGES)
            fb[page * SSD1306_WIDTH + x + i] |= col << bit;
        if (bit && page + 1 < SSD1306_PAGES)
            fb[(page + 1) * SSD1306_WIDTH + x + i] |= col >> (8 - bit);
    }
}

void ssd1306_draw_string(uint8_t x, uint8_t y, const char *str) {
    while (*str) {
        ssd1306_draw_char(x, y, *str++);
        x += 6;
        if (x > SSD1306_WIDTH - 6) break;
    }
}

void ssd1306_invert(bool invert) {
    cmd(invert ? 0xA7 : 0xA6);
}

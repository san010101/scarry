#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

// =============================================================================
// 1. НАЗНАЧЕНИЕ ВЫВОДОВ RP2040 (A1) ПО СХЕМЕ ПЛАТЫ alf-scar
// =============================================================================
#define PIN_BTN_DOWN    3   // Вывод 5 A1  (/BTN_DOWN, кнопка SW3)
#define PIN_I2C_SDA     4   // Вывод 6 A1  (/I2C_SDA, дисплей U8)
#define PIN_I2C_SCL     5   // Вывод 7 A1  (/I2C_SCL, дисплей U8)
#define PIN_BTN_OK      7   // Вывод 10 A1 (/BTN_OK, кнопка SW2)
#define PIN_SD_MISO     8   // Вывод 11 A1 (/SD_MISO, слот J2 DAT0)
#define PIN_Z80_RESET   9   // Вывод 12 A1 (/Z80_RESET, база Q1)
#define PIN_SD_SCK      10  // Вывод 14 A1 (/SD_SCK, слот J2 CLK)
#define PIN_SD_MOSI     11  // Вывод 15 A1 (/SD_MOSI, слот J2 CMD)
#define PIN_BTN_UP      12  // Вывод 16 A1 (/BTN_UP, кнопка SW1)
#define PIN_SD_CS       14  // Вывод 19 A1 (/SD_CS, слот J2 DAT3)

#define PIN_CPLD_MISO   16  // Вывод 21 A1 (/SPI_MISO, U7 пин 47)
#define PIN_CPLD_CS     17  // Вывод 22 A1 (/SPI_CS, U7 пин 48)
#define PIN_CPLD_SCK    18  // Вывод 24 A1 (/SPI_SCK, U7 пин 49)
#define PIN_CPLD_MOSI   19  // Вывод 25 A1 (/SPI_MOSI, U7 пин 50)
#define PIN_Z80_RST_IN  20  // Вывод 26 A1 (/Z80_RST_33V, выход Y6 U4)
#define PIN_CLK_50MHZ   21  // Вывод 27 A1 (/CLK_50MHZ, вход GCLK0 U7)
#define PIN_MCU_ACTIVE  22  // Вывод 29 A1 (/MCU_ACTIVE, вход IO1_35 U7)

#define OLED_I2C_ADDR   0x3C

// Режимы работы памяти в ПЛИС
enum MemMode {
    MEM_AUTO = 0, // Автоматически: до 256КБ включает 256K, свыше 256КБ включает 512K
    MEM_256K = 1, // Принудительно 256 КБ (отсечка A18=1)
    MEM_512K = 2  // Принудительно 512 КБ
};

// Глобальные настройки из файла config.ini
static MemMode current_mem_mode   = MEM_AUTO;
static bool    cfg_play_intro     = true;  // Показывать ли стартовый мультфильм с Альфом
static bool    cfg_show_tests     = true;  // Показывать ли сервисные пункты в меню
static bool    cfg_bist_boot      = false; // BIST при старте (включается через BIST_BOOT=1 в config.ini)
static bool    cfg_autostart      = true;  // По умолчанию ВКЛЮЧЕНО: сразу монтировать образ при включении!
static int     cfg_saver_min      = 3;     // Таймер скринсейвера в минутах (3 мин по умолчанию, 0 = выкл)
static char    cfg_target_rom[64] = {0};   // Имя выбранного образа игры

// Координаты сектора файла config.ini на SD-карте
static uint32_t cfg_data_lba  = 0;
static uint32_t cfg_dir_lba   = 0;
static int      cfg_dir_off   = 0;
static uint32_t cfg_file_size = 0;

// =============================================================================
// 2. АНИМАЦИОННЫЙ ДВИЖОК АЛЬФА (6 КАДРОВ 32x32 + СПРАЙТ УБЕГАЮЩЕГО КОТА 16x16)
// =============================================================================
static const uint32_t sprite_alf_32x32[6][32] = {
    // КАДР 0: Базовый портрет Альфа (чубчик, уши, складки на носу, хитрая ухмылка)
    {
        0b00000000000111111100000000000000,
        0b00000000011111111111000000000000,
        0b00000000111111001111100000000000,
        0b00000001111111111111000000000000,
        0b00000011111111111111100000000000,
        0b01110011100001111000011100111000,
        0b11111011011111111111101101111100,
        0b11001111111111111111111111001100,
        0b11011111000011111100001111101100,
        0b01110110011001111001100110111000,
        0b00111110011001111001100111110000,
        0b00011111100011111100011111100000,
        0b00001111111111111111111111000000,
        0b00011111100000000000011111100000,
        0b00111111111111111111111111110000,
        0b00111111000000000000001111110000,
        0b01111111111111111111111111111000,
        0b01111110000000000000000111111000,
        0b01111111111111111111111111111000,
        0b01111011110001111000111101111000,
        0b00111011110001111000111101110000,
        0b00111001111111111111111001110000,
        0b00011100111111111111110011100000,
        0b00001110000000000000000111000000,
        0b00000111111111111111111110000000,
        0b00000011111111111111111100000000,
        0b00000001111111111111111000000000,
        0b00000111111111111111111110000000,
        0b00011111111111111111111111100000,
        0b00111111111111111111111111110000,
        0b01111111111111111111111111111000,
        0b11111111111111111111111111111100
    },

    // КАДР 1: Принюхивается (чубчик влево, левое ухо вверх, нос приподнят)
    {
        0b00000000001111111000000000000000,
        0b00000000111111111110000000000000,
        0b00000001111110011110000000000000,
        0b00000001111111111111000000000000,
        0b01110011111111111111100000000000,
        0b11111011100001111000011100111000,
        0b11001111011111111111101101111100,
        0b11011111111111111111111111001100,
        0b01110111000011111100001111101100,
        0b00111110011001111001100110111000,
        0b00011110011001111001100111110000,
        0b00001111100011111100011111100000,
        0b00011111100000000000011111100000,
        0b00111111111111111111111111110000,
        0b00111111000000000000001111110000,
        0b01111111111111111111111111111000,
        0b01111110000000000000000111111000,
        0b01111111111111111111111111111000,
        0b01111011110001111000111101111000,
        0b00111011110001111000111101110000,
        0b00111001111111111111111001110000,
        0b00011100111111111111110011100000,
        0b00001110000000000000000111000000,
        0b00000111111111111111111110000000,
        0b00000011111111111111111100000000,
        0b00000011111111111111111100000000,
        0b00000001111111111111111000000000,
        0b00000111111111111111111110000000,
        0b00011111111111111111111111100000,
        0b00111111111111111111111111110000,
        0b01111111111111111111111111111000,
        0b11111111111111111111111111111100
    },

    // КАДР 2: Моргание (тяжелые веки опущены)
    {
        0b00000000000111111100000000000000,
        0b00000000011111111111000000000000,
        0b00000000111111001111100000000000,
        0b00000001111111111111000000000000,
        0b00000011111111111111100000000000,
        0b01110011100001111000011100111000,
        0b11111011011111111111101101111100,
        0b11001111111111111111111111001100,
        0b11011111111111111111111111101100,
        0b01110111111111111111111110111000,
        0b00111110000001111000000111110000,
        0b00011111111111111111111111100000,
        0b00001111111111111111111111000000,
        0b00011111100000000000011111100000,
        0b00111111111111111111111111110000,
        0b00111111000000000000001111110000,
        0b01111111111111111111111111111000,
        0b01111110000000000000000111111000,
        0b01111111111111111111111111111000,
        0b01111011110001111000111101111000,
        0b00111011110001111000111101110000,
        0b00111001111111111111111001110000,
        0b00011100111111111111110011100000,
        0b00001110000000000000000111000000,
        0b00000111111111111111111110000000,
        0b00000011111111111111111100000000,
        0b00000001111111111111111000000000,
        0b00000111111111111111111110000000,
        0b00011111111111111111111111100000,
        0b00111111111111111111111111110000,
        0b01111111111111111111111111111000,
        0b11111111111111111111111111111100
    },

    // КАДР 3: Хитрое подмигивание и приоткрытая пасть с клыками
    {
        0b00000000000111111110000000000000,
        0b00000000011111111111100000000000,
        0b00000000111111000111110000000000,
        0b00000001111111111111000000000000,
        0b00000011111111111000011100111000,
        0b01110011100001110111101101111100,
        0b11111011011111111111111111001100,
        0b11001111111111111100001111101100,
        0b11011111111111111001100110111000,
        0b01110110000001111001100111110000,
        0b00111111111111111100001111100000,
        0b00011111111111111111111111100000,
        0b00001111111111111111111111000000,
        0b00011111100000000000011111100000,
        0b00111111111111111111111111110000,
        0b00111111000000000000001111110000,
        0b01111111111111111111111111111000,
        0b01111110000000000000000111111000,
        0b01111111111111111111111111111000,
        0b01111011110001111000111101111000,
        0b00111011110001111000111101110000,
        0b00111001111111111111111001110000,
        0b00011100111111111111110011100000,
        0b00001110000000000000000111000000,
        0b00001110011000000001100111000000,
        0b00000111111111111111111110000000,
        0b00000011111111111111111100000000,
        0b00000111111111111111111110000000,
        0b00011111111111111111111111100000,
        0b00111111111111111111111111110000,
        0b01111111111111111111111111111000,
        0b11111111111111111111111111111100
    },

    // КАДР 4: Фирменный смех Альфа "HA! HA!" (широко открытая пасть и нижние клыки)
    {
        0b00000000011111111000000000000000,
        0b00000001111111111110000000000000,
        0b00000011111100111111000000000000,
        0b00000011111111111110000000000000,
        0b01110011100001111000011100111000,
        0b11111011011111111111101101111100,
        0b11001111111111111111111111001100,
        0b11011111000011111100001111101100,
        0b01110110011001111001100110111000,
        0b00111110011001111001100111110000,
        0b00011111100011111100011111100000,
        0b00001111111111111111111111000000,
        0b00011111100000000000011111100000,
        0b00111111111111111111111111110000,
        0b00111111000000000000001111110000,
        0b01111111111111111111111111111000,
        0b01111110000000000000000111111000,
        0b01111111111111111111111111111000,
        0b01111011110001111000111101111000,
        0b00111011110001111000111101110000,
        0b00111001111111111111111001110000,
        0b00011100111111111111110011100000,
        0b00001110000000000000000111000000,
        0b00001110000000000000000111000000,
        0b00001110011000000001100111000000,
        0b00001110011001111001100111000000,
        0b00000111111111111111111110000000,
        0b00000011111111111111111100000000,
        0b00000111111111111111111110000000,
        0b00011111111111111111111111100000,
        0b00111111111111111111111111110000,
        0b11111111111111111111111111111100
    },

    // КАДР 5: Вторая фаза смеха (голова чуть откинута, уши вразлет)
    {
        0b00000000000111111110000000000000,
        0b00000000011111111111100000000000,
        0b00000000111111000111110000000000,
        0b11100001111111111111000000011100,
        0b11111011100001111000011101111100,
        0b11001111011111111111101111001100,
        0b11011111111111111111111111101100,
        0b01110111000011111100001110111000,
        0b00111110011001111001100111110000,
        0b00011110011001111001100111100000,
        0b00001111100011111100011111000000,
        0b00001111111111111111111111000000,
        0b00011111100000000000011111100000,
        0b00111111111111111111111111110000,
        0b00111111000000000000001111110000,
        0b01111111111111111111111111111000,
        0b01111110000000000000000111111000,
        0b01111111111111111111111111111000,
        0b01111011110001111000111101111000,
        0b00111011110001111000111101110000,
        0b00111001111111111111111001110000,
        0b00011100111111111111110011100000,
        0b00001110000000000000000111000000,
        0b00001110011000000001100111000000,
        0b00001110011001111001100111000000,
        0b00000111111111111111111110000000,
        0b00000011111111111111111100000000,
        0b00000001111111111111111000000000,
        0b00000111111111111111111110000000,
        0b00011111111111111111111111100000,
        0b00111111111111111111111111110000,
        0b11111111111111111111111111111100
    }
};

// Спрайт убегающего кота Лаки 16x16 (для стартового мультфильма и скринсейвера)
static const uint8_t sprite_running_cat[2][32] = {
    {
        0x40, 0x04, 0x60, 0x0C, 0x70, 0x1C, 0x7F, 0xFC,
        0x40, 0x04, 0x52, 0x4C, 0x52, 0x4C, 0x40, 0x04,
        0x41, 0x84, 0x40, 0x04, 0x24, 0x29, 0x1F, 0xF1,
        0x10, 0x12, 0x14, 0x54, 0x24, 0x48, 0x40, 0x04
    },
    {
        0x40, 0x04, 0x60, 0x0C, 0x70, 0x1C, 0x7F, 0xFC,
        0x40, 0x04, 0x52, 0x4C, 0x52, 0x4C, 0x40, 0x04,
        0x41, 0x84, 0x40, 0x04, 0x24, 0x28, 0x1F, 0xF3,
        0x10, 0x14, 0x0A, 0x28, 0x04, 0x10, 0x08, 0x20
    }
};

// Режиссерская раскадровка мимики Альфа (16 шагов по 220 мс)
static const uint8_t alf_timeline[16] = {
    0, 0, 1, 0,  2, 0, 1, 0,  3, 3, 0, 4,  5, 4, 5, 0
};

// Коронные реплики Альфа из сериала
static const char* const alf_quotes[6] = {
    "ALF: NO PROBLEM!",
    "ALF: HA! HA! HA!",
    "ALF: YO WILLY!",
    "ALF: HERE KITTY!",
    "ALF: MELMAC ROCKS",
    "ALF: YUM! CAT PIE"
};

// Всплывающие реплики для скринсейвера
static const char* const saver_bubbles[4] = {
    "HA! HA! HA!",
    "NO PROBLEM!",
    "YO WILLY!",
    "WHERE'S CAT?"
};

// =============================================================================
// 3. ДРАЙВЕР OLED U8 (SSD1306 128x32), КОНТРАСТ И БЕГУЩАЯ СТРОКА
// =============================================================================
static uint8_t vram[128 * 32 / 8];

static void oled_write_cmd(uint8_t cmd) {
    uint8_t packet[2] = {0x00, cmd};
    i2c_write_blocking(i2c0, OLED_I2C_ADDR, packet, 2, false);
}

void oled_init() {
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    sleep_ms(40);

    const uint8_t seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
        0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    for (size_t i = 0; i < sizeof(seq); i++) oled_write_cmd(seq[i]);
    memset(vram, 0, sizeof(vram));
}

void oled_set_contrast(uint8_t level) {
    oled_write_cmd(0x81);
    oled_write_cmd(level);
}

void oled_clear() {
    memset(vram, 0, sizeof(vram));
}

void oled_flush() {
    oled_write_cmd(0x21); oled_write_cmd(0); oled_write_cmd(127);
    oled_write_cmd(0x22); oled_write_cmd(0); oled_write_cmd(3);
    uint8_t tx[513];
    tx[0] = 0x40;
    memcpy(&tx[1], vram, 512);
    i2c_write_blocking(i2c0, OLED_I2C_ADDR, tx, sizeof(tx), false);
}

void oled_put_pixel(int x, int y, bool color) {
    if (x < 0 || x >= 128 || y < 0 || y >= 32) return;
    if (color) vram[x + (y >> 3) * 128] |=  (1 << (y & 7));
    else       vram[x + (y >> 3) * 128] &= ~(1 << (y & 7));
}

void oled_draw_icon16(int x, int y, const uint8_t* icon, bool flip_x = false) {
    for (int r = 0; r < 16; r++) {
        uint16_t bits = (icon[r * 2] << 8) | icon[r * 2 + 1];
        for (int c = 0; c < 16; c++) {
            int bit_idx = flip_x ? (15 - c) : c;
            if (bits & (0x8000U >> bit_idx)) oled_put_pixel(x + c, y + r, true);
        }
    }
}

void oled_draw_alf32(int x, int y, int frame_idx, bool draw_divider = true, bool flip_x = false) {
    if (draw_divider && x == 96) {
        for (int r = 0; r < 32; r += 2) {
            oled_put_pixel(94, r, true);
        }
    }
    const uint32_t* rows = sprite_alf_32x32[frame_idx % 6];
    for (int r = 0; r < 32; r++) {
        uint32_t row_bits = rows[r];
        for (int c = 0; c < 32; c++) {
            int bit_idx = flip_x ? (31 - c) : c;
            if (row_bits & (0x80000000UL >> bit_idx)) {
                oled_put_pixel(x + c, y + r, true);
            }
        }
    }
}

static uint16_t char_bitmap_3x5(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    switch (c) {
        case '0': return 075557; case '1': return 026227; case '2': return 071747;
        case '3': return 071717; case '4': return 055711; case '5': return 074717;
        case '6': return 074757; case '7': return 071122; case '8': return 075757;
        case '9': return 075717; case 'A': return 025755; case 'B': return 065656;
        case 'C': return 034443; case 'D': return 065556; case 'E': return 074747;
        case 'F': return 074744; case 'G': return 034553; case 'H': return 055755;
        case 'I': return 072227; case 'J': return 011152; case 'K': return 055655;
        case 'L': return 044447; case 'M': return 057755; case 'N': return 057555;
        case 'O': return 025552; case 'P': return 065644; case 'Q': return 025573;
        case 'R': return 065655; case 'S': return 034216; case 'T': return 072222;
        case 'U': return 055557; case 'V': return 055552; case 'W': return 055775;
        case 'X': return 055255; case 'Y': return 055222; case 'Z': return 071247;
        case ':': return 002020; case '-': return 000700; case '.': return 000002;
        case ',': return 000024;
        case '[': return 064446; case ']': return 031113; case '>': return 042124;
        case '!': return 022202; case '?': return 071202; case '\'': return 022000;
        case '/': return 011244; case '_': return 000007;
        case '(': return 012221; case ')': return 042224; case '+': return 002720;
        default:  return 000000;
    }
}

void oled_print_clipped(int x, int y, int x_min, int x_max, const char* s) {
    while (*s) {
        if (x > x_max) break;
        uint16_t g = char_bitmap_3x5(*s++);
        if (x + 3 >= x_min) {
            for (int r = 0; r < 5; r++) {
                uint8_t row = (g >> ((4 - r) * 3)) & 0x07;
                for (int c = 0; c < 3; c++) {
                    int px = x + c;
                    if (px >= x_min && px <= x_max && (row & (1 << (2 - c)))) {
                        oled_put_pixel(px, y + r, true);
                    }
                }
            }
        }
        x += 4;
    }
}

void oled_print(int x, int y, const char* s) {
    oled_print_clipped(x, y, 0, 127, s);
}

void oled_print_marquee(int x_min, int x_max, int y, const char* s, bool active, uint32_t now_ms) {
    int text_px = (int)strlen(s) * 4;
    int win_px  = x_max - x_min + 1;

    if (!active || text_px <= win_px) {
        oled_print_clipped(x_min, y, x_min, x_max, s);
        return;
    }

    int max_scroll = text_px - win_px;
    int cycle_px   = max_scroll + 40;
    int step       = (now_ms / 45) % cycle_px;
    int offset     = step - 15;
    if (offset < 0) offset = 0;
    if (offset > max_scroll) offset = max_scroll;

    oled_print_clipped(x_min - offset, y, x_min, x_max, s);
}

void oled_progress(int y, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    for (int x = 0; x < 92; x++) {
        oled_put_pixel(x, y, true);
        oled_put_pixel(x, y + 6, true);
        bool fill = (x <= (pct * 91) / 100);
        for (int h = 1; h < 6; h++) oled_put_pixel(x, y + h, fill);
    }
}

// Трехфазный скринсейвер с погоней Альфа за котиком Лаки и патрулированием
void render_alf_screensaver(uint32_t elapsed_ms, int base_alf_frame) {
    int step = (elapsed_ms / 45) % 240;
    oled_clear();

    if (step < 60) {
        int cat_x = -16 + step * 3;
        int alf_x = cat_x - 36;
        int cat_y = 6 + ((step / 3) % 3) * 5 + (step & 1);
        int alf_y = ((step / 5) % 3) - 1;
        int chomp_frame = (step & 2) ? 4 : ((step & 1) ? 3 : 0);

        int text_y = (cat_y > 12) ? 2 : 25;
        oled_print(18 + (step / 3), text_y, (step < 30) ? "HERE KITTY!" : "WAIT, LUCKY!");

        oled_draw_icon16(cat_x, cat_y, sprite_running_cat[step & 1], false);
        oled_draw_alf32(alf_x, alf_y, chomp_frame, false, false);

    } else if (step < 120) {
        int s     = step - 60;
        int cat_x = 132 - s * 3;
        int alf_x = cat_x + 20;
        int cat_y = 4 + (((s + 2) / 3) % 3) * 5 + (s & 1);
        int alf_y = ((s / 5) % 3) - 1;
        int chomp_frame = (s & 2) ? 4 : ((s & 1) ? 3 : 0);

        int text_y = (cat_y > 12) ? 2 : 25;
        oled_print(40 - (s / 3), text_y, (s < 30) ? "COME BACK!" : "YUM! CAT PIE");

        oled_draw_icon16(cat_x, cat_y, sprite_running_cat[s & 1], true);
        oled_draw_alf32(alf_x, alf_y, chomp_frame, false, true);

    } else {
        int s        = step - 120;
        int saver_x  = (s < 60) ? ((s * 96) / 60) : (((120 - s) * 96) / 60);
        int saver_y  = ((elapsed_ms / 450) % 3) - 1;
        bool go_left = (s >= 60);
        int frame    = base_alf_frame;

        if (saver_x <= 10 || saver_x >= 86) {
            frame = ((elapsed_ms / 140) & 1) ? 4 : 5;
            int bubble_idx = ((elapsed_ms / 2500) % 4);
            int bubble_y   = 4 + ((elapsed_ms / 1200) % 3) * 10;
            int bubble_x   = (saver_x < 48) ? 38 : 4;
            oled_print(bubble_x, bubble_y, saver_bubbles[bubble_idx]);
        }

        oled_draw_alf32(saver_x, saver_y, frame, false, go_left);
    }

    oled_flush();
}

// =============================================================================
// 4. SPI-МОСТ К ПЛИС U7 (EPM240T100) И УПРАВЛЕНИЕ SRAM U6 (AS6C4008)
// =============================================================================
void cpld_bridge_init() {
    gpio_init(PIN_Z80_RESET);
    gpio_set_dir(PIN_Z80_RESET, GPIO_OUT);
    gpio_put(PIN_Z80_RESET, 1);

    gpio_init(PIN_MCU_ACTIVE);
    gpio_set_dir(PIN_MCU_ACTIVE, GPIO_OUT);
    gpio_put(PIN_MCU_ACTIVE, 1);

    clock_gpio_init(PIN_CLK_50MHZ, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 2);

    spi_init(spi0, 8 * 1000 * 1000);
    gpio_set_function(PIN_CPLD_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CPLD_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_CPLD_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CPLD_CS);
    gpio_set_dir(PIN_CPLD_CS, GPIO_OUT);
    gpio_put(PIN_CPLD_CS, 1);
}

void cpld_set_256k_mode(bool enable_256k) {
    uint8_t pkt[2] = { 0x03, (uint8_t)(enable_256k ? 0x01 : 0x00) };
    gpio_put(PIN_CPLD_CS, 0);
    spi_write_blocking(spi0, pkt, 2);
    gpio_put(PIN_CPLD_CS, 1);
}

void sram_write_buf(uint32_t addr, const uint8_t* data, size_t len) {
    uint8_t hdr[4] = {
        0x01,
        (uint8_t)((addr >> 16) & 0x07),
        (uint8_t)((addr >> 8)  & 0xFF),
        (uint8_t)(addr & 0xFF)
    };
    gpio_put(PIN_CPLD_CS, 0);
    spi_write_blocking(spi0, hdr, 4);
    spi_write_blocking(spi0, data, len);
    gpio_put(PIN_CPLD_CS, 1);
}

void sram_read_buf(uint32_t addr, uint8_t* data, size_t len) {
    uint8_t hdr[4] = {
        0x02,
        (uint8_t)((addr >> 16) & 0x07),
        (uint8_t)((addr >> 8)  & 0xFF),
        (uint8_t)(addr & 0xFF)
    };
    gpio_put(PIN_CPLD_CS, 0);
    spi_write_blocking(spi0, hdr, 4);
    spi_read_blocking(spi0, 0x00, data, len);
    gpio_put(PIN_CPLD_CS, 1);
}

static inline void sram_poke(uint32_t a, uint8_t v) { sram_write_buf(a, &v, 1); }
static inline uint8_t sram_peek(uint32_t a) { uint8_t v = 0; sram_read_buf(a, &v, 1); return v; }

// =============================================================================
// 5. ТЕСТОВАЯ ЧАСТЬ: BIST (ПРОВЕРКА ЦЕПИ СБРОСА, ШИН ДАННЫХ/АДРЕСА И 512 КБ)
// =============================================================================
bool run_hardware_bist(char* report, size_t max_len) {
    gpio_put(PIN_Z80_RESET, 1);
    gpio_put(PIN_MCU_ACTIVE, 1);
    cpld_set_256k_mode(false);
    sleep_ms(5);

    if (gpio_get(PIN_Z80_RST_IN) != 0) {
        snprintf(report, max_len, "ERR: RST Q1/U4");
        return false;
    }

    for (int b = 0; b < 8; b++) {
        uint8_t p1 = (1U << b);
        uint8_t p0 = ~p1;
        sram_poke(0x00000, p1);
        if (sram_peek(0x00000) != p1) {
            snprintf(report, max_len, "ERR DATA BIT D%d", b);
            return false;
        }
        sram_poke(0x00000, p0);
        if (sram_peek(0x00000) != p0) {
            snprintf(report, max_len, "ERR DATA BIT D%d", b);
            return false;
        }
    }

    sram_poke(0x00000, 0xAA);
    for (int a = 0; a < 19; a++) sram_poke(1UL << a, (uint8_t)(0x40 + a));
    if (sram_peek(0x00000) != 0xAA) {
        snprintf(report, max_len, "ERR ADDR0 SHORT");
        return false;
    }
    for (int a = 0; a < 19; a++) {
        if (sram_peek(1UL << a) != (uint8_t)(0x40 + a)) {
            snprintf(report, max_len, "ERR ADDR LINE A%d", a);
            return false;
        }
    }

    uint8_t tx[256], rx[256];
    for (uint32_t page = 0; page < 0x80000; page += 8192) {
        int step = (page >> 13) & 3;
        oled_clear();
        oled_print(0, 2, "BIST: SRAM 512K");
        oled_progress(14, (page * 100) / 0x80000);
        oled_print(0, 25, "ALF CHECKING...");
        oled_draw_alf32(96, 0, (step == 2) ? 2 : (step & 1));
        oled_flush();

        for (int i = 0; i < 256; i++) tx[i] = (uint8_t)((page >> 13) ^ i ^ 0xA5);
        sram_write_buf(page, tx, 256);
        sram_read_buf(page, rx, 256);
        if (memcmp(tx, rx, 256) != 0) {
            snprintf(report, max_len, "ERR @ 0X%05lX", page);
            return false;
        }
    }

    snprintf(report, max_len, "BIST: 512KB OK!");
    return true;
}

// =============================================================================
// 6. ДРАЙВЕР MICROSD (SPI1): АВТООПРЕДЕЛЕНИЕ ВСТАВКИ/ИЗВЛЕЧЕНИЯ И config.ini
// =============================================================================
static bool     sd_hc         = false;
static bool     sd_card_ready = false;
static uint32_t fat_lba       = 0;
static uint32_t fat_size      = 0;
static uint8_t  fat_count     = 2;
static uint32_t data_lba      = 0;
static uint32_t spc           = 0;
static uint32_t root_clus     = 2;

static inline void sd_sel()   { gpio_put(PIN_SD_CS, 0); }
static inline void sd_desel() { gpio_put(PIN_SD_CS, 1); uint8_t d = 0xFF; spi_write_blocking(spi1, &d, 1); }

static uint8_t sd_io(uint8_t out) {
    uint8_t in = 0xFF;
    spi_write_read_blocking(spi1, &out, &in, 1);
    return in;
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    sd_io(0xFF);
    sd_io(0x40 | cmd);
    sd_io((arg >> 24) & 0xFF);
    sd_io((arg >> 16) & 0xFF);
    sd_io((arg >> 8)  & 0xFF);
    sd_io(arg & 0xFF);
    sd_io(crc);
    for (int i = 0; i < 10; i++) {
        uint8_t r = sd_io(0xFF);
        if ((r & 0x80) == 0) return r;
    }
    return 0xFF;
}

// Быстрый неблокирующий опрос пустого слота (~2 мс): проверяет, вставили ли карту
bool sd_fast_detect_insertion() {
    spi_init(spi1, 250 * 1000);
    gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_SD_MISO);

    gpio_init(PIN_SD_CS);
    gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    gpio_put(PIN_SD_CS, 1);

    int idle_ff = 0;
    for (int i = 0; i < 10; i++) {
        if (sd_io(0xFF) == 0xFF) idle_ff++;
    }
    if (idle_ff < 8) return false;

    sd_sel();
    bool detected = false;
    for (int i = 0; i < 4; i++) {
        uint8_t r = sd_cmd(0, 0, 0x95);
        if (r == 0x01) {
            detected = true;
            break;
        }
    }
    sd_desel();
    return detected;
}

// Быстрая проверка (~50 мкс), что ранее инициализированная карта всё еще в слоте
bool sd_fast_check_present() {
    if (!sd_card_ready) return false;

    for (int retry = 0; retry < 2; retry++) {
        sd_sel();
        uint8_t r = sd_cmd(58, 0, 0xFD);
        uint8_t ocr[4] = {0, 0, 0, 0};
        if (r == 0x00) {
            for (int i = 0; i < 4; i++) ocr[i] = sd_io(0xFF);
        }
        sd_desel();

        if (r == 0x00) {
            uint32_t ocr32 = ((uint32_t)ocr[0] << 24) | ((uint32_t)ocr[1] << 16) |
                             ((uint32_t)ocr[2] << 8)  | (uint32_t)ocr[3];
            if (ocr32 != 0x00000000UL && ocr32 != 0xFFFFFFFFUL && (ocr32 & 0x00FF8000UL) != 0) {
                return true;
            }
        }
    }

    sd_card_ready = false;
    return false;
}

// Полная инициализация и проверка подлинности microSD-карты в слоте J2
bool sd_init_spi() {
    sd_card_ready = false;

    spi_init(spi1, 250 * 1000);
    gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_SD_MISO);

    gpio_init(PIN_SD_CS);
    gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    gpio_put(PIN_SD_CS, 1);

    sleep_ms(80);

    int idle_ff_count = 0;
    for (int i = 0; i < 20; i++) {
        if (sd_io(0xFF) == 0xFF) idle_ff_count++;
    }
    if (idle_ff_count < 16) {
        return false;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        sd_sel();
        uint8_t r = 0xFF;
        for (int i = 0; i < 40; i++) {
            r = sd_cmd(0, 0, 0x95);
            if (r == 0x01) break;
            sleep_ms(3);
        }

        if (r == 0x01) {
            uint8_t r8 = sd_cmd(8, 0x1AA, 0x87);
            bool v2_card    = false;
            bool cmd8_valid = false;

            if (r8 == 0x01) {
                uint8_t r7[4];
                for (int i = 0; i < 4; i++) r7[i] = sd_io(0xFF);
                if ((r7[2] & 0x0F) == 0x01 && r7[3] == 0xAA) {
                    v2_card    = true;
                    cmd8_valid = true;
                }
            } else if (r8 == 0x05) {
                cmd8_valid = true;
            }

            if (cmd8_valid) {
                uint32_t t0 = to_ms_since_boot(get_absolute_time());
                do {
                    sd_cmd(55, 0, 0x65);
                    r = sd_cmd(41, v2_card ? 0x40000000UL : 0x00000000UL, 0x77);
                    if (to_ms_since_boot(get_absolute_time()) - t0 > 1000) break;
                } while (r != 0x00);

                if (r == 0x00 && sd_cmd(58, 0, 0xFD) == 0x00) {
                    uint8_t ocr[4];
                    for (int i = 0; i < 4; i++) ocr[i] = sd_io(0xFF);
                    uint32_t ocr32 = ((uint32_t)ocr[0] << 24) | ((uint32_t)ocr[1] << 16) |
                                     ((uint32_t)ocr[2] << 8)  | (uint32_t)ocr[3];

                    if (ocr32 != 0x00000000UL && ocr32 != 0xFFFFFFFFUL && (ocr32 & 0x00FF8000UL) != 0) {
                        sd_hc         = (ocr[0] & 0x40) != 0;
                        sd_card_ready = true;
                        sd_desel();
                        spi_set_baudrate(spi1, 8 * 1000 * 1000);
                        return true;
                    }
                }
            }
        }
        sd_desel();
        sleep_ms(30);
    }
    return false;
}

bool sd_read_sec(uint32_t lba, uint8_t* buf) {
    if (!sd_card_ready) return false;
    uint32_t addr = sd_hc ? lba : (lba * 512UL);
    sd_sel();
    if (sd_cmd(17, addr, 0xFF) != 0x00) { sd_desel(); return false; }
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (sd_io(0xFF) != 0xFE) {
        if (to_ms_since_boot(get_absolute_time()) - t0 > 250) { sd_desel(); return false; }
    }
    spi_read_blocking(spi1, 0xFF, buf, 512);
    sd_io(0xFF); sd_io(0xFF);
    sd_desel();
    return true;
}

bool sd_write_sec(uint32_t lba, const uint8_t* buf) {
    if (!sd_card_ready || lba == 0) return false;
    uint32_t addr = sd_hc ? lba : (lba * 512UL);
    sd_sel();
    if (sd_cmd(24, addr, 0xFF) != 0x00) {
        sd_desel();
        return false;
    }
    sd_io(0xFF);
    sd_io(0xFE);
    spi_write_blocking(spi1, buf, 512);
    sd_io(0xFF);
    sd_io(0xFF);

    uint8_t resp = sd_io(0xFF);
    if ((resp & 0x1F) != 0x05) {
        sd_desel();
        return false;
    }

    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (sd_io(0xFF) != 0xFF) {
        if (to_ms_since_boot(get_absolute_time()) - t0 > 500) {
            sd_desel();
            return false;
        }
    }
    sd_desel();
    return true;
}

bool sd_check_card_alive() {
    if (!sd_fast_check_present()) return false;
    uint8_t sec0[512];
    memset(sec0, 0, sizeof(sec0));
    if (!sd_read_sec(0, sec0)) {
        sd_card_ready = false;
        return false;
    }
    if (sec0[510] != 0x55 || sec0[511] != 0xAA) {
        sd_card_ready = false;
        return false;
    }
    return true;
}

static uint16_t u16_le(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t u32_le(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

static void wr16_le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void wr32_le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

enum ItemType {
    ITEM_HW_TEST = 0,
    ITEM_MEM_MODE,
    ITEM_AUTOMOUNT,
    ITEM_SAVER,
    ITEM_ROM,
    ITEM_INFO
};

struct FileItem {
    ItemType type;
    char     title[80];    // Строка для экрана: "[256K] DIZZY.BIN"
    char     raw_name[64]; // Чистое имя файла для записи в config.ini: "DIZZY.BIN"
    uint32_t cluster;
    uint32_t size;
};

#define MAX_ITEMS 64
static FileItem items[MAX_ITEMS];
static int item_count = 0;

void update_service_menu_titles() {
    for (int i = 0; i < item_count; i++) {
        if (items[i].type == ITEM_MEM_MODE) {
            if (current_mem_mode == MEM_AUTO)      strcpy(items[i].title, "[ MEM: AUTO 256/512 ]");
            else if (current_mem_mode == MEM_256K) strcpy(items[i].title, "[ MEM: FORCE 256KB ]");
            else                                   strcpy(items[i].title, "[ MEM: FORCE 512KB ]");
        } else if (items[i].type == ITEM_AUTOMOUNT) {
            strcpy(items[i].title, cfg_autostart ? "[ AUTO MOUNT: ON ]" : "[ AUTO MOUNT: OFF ]");
        }
    }
}

static uint32_t clus_to_lba(uint32_t c) { return data_lba + (c - 2UL) * spc; }

static uint32_t next_clus(uint32_t c) {
    uint8_t sec[512];
    if (!sd_read_sec(fat_lba + (c * 4UL) / 512UL, sec)) return 0x0FFFFFFFUL;
    return u32_le(&sec[(c * 4UL) % 512UL]) & 0x0FFFFFFFUL;
}

static void extract_lfn_part(const uint8_t* entry, char* lfn_buf) {
    static const uint8_t lfn_offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    int seq = (entry[0] & 0x1F) - 1;
    if (seq < 0 || seq > 4) return;
    int base = seq * 13;
    for (int i = 0; i < 13; i++) {
        if (base + i >= 63) break;
        uint8_t lo = entry[lfn_offsets[i]];
        uint8_t hi = entry[lfn_offsets[i] + 1];
        if (lo == 0x00 && hi == 0x00) {
            lfn_buf[base + i] = '\0';
            break;
        }
        lfn_buf[base + i] = (hi == 0x00 && lo >= 32 && lo < 127) ? (char)lo : '_';
    }
}

static char* trim_spaces(char* s) {
    while (*s && (*s == ' ' || *s == '\t' || *s == '\"' || *s == '\'')) s++;
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == '\"' || s[len - 1] == '\'')) {
        s[--len] = '\0';
    }
    return s;
}

static bool parse_bool_val(const char* val) {
    return (strcasecmp(val, "1") == 0 || strcasecmp(val, "YES") == 0 ||
            strcasecmp(val, "TRUE") == 0 || strcasecmp(val, "ON") == 0);
}

static void parse_config_text(char* text) {
    char* line = text;
    while (line && *line) {
        char* next_line = strpbrk(line, "\r\n");
        if (next_line) {
            *next_line = '\0';
            next_line++;
            while (*next_line == '\r' || *next_line == '\n') next_line++;
        }

        char* clean = trim_spaces(line);
        if (clean[0] != '\0' && clean[0] != '#' && clean[0] != ';' && clean[0] != '[' && clean[0] != '/') {
            char* eq = strchr(clean, '=');
            if (eq) {
                *eq = '\0';
                char* key = trim_spaces(clean);
                char* val = trim_spaces(eq + 1);

                if (strcasecmp(key, "ROM") == 0 || strcasecmp(key, "GAME") == 0) {
                    strncpy(cfg_target_rom, val, sizeof(cfg_target_rom) - 1);
                    cfg_target_rom[sizeof(cfg_target_rom) - 1] = '\0';
                } else if (strcasecmp(key, "AUTO_MOUNT") == 0 || strcasecmp(key, "AUTOMOUNT") == 0) {
                    cfg_autostart = parse_bool_val(val);
                } else if (strcasecmp(key, "INTRO") == 0 || strcasecmp(key, "ALF_INTRO") == 0 || strcasecmp(key, "CARTOON") == 0) {
                    cfg_play_intro = parse_bool_val(val);
                } else if (strcasecmp(key, "SHOW_TESTS") == 0 || strcasecmp(key, "TESTS") == 0) {
                    cfg_show_tests = parse_bool_val(val);
                } else if (strcasecmp(key, "BIST_BOOT") == 0 || strcasecmp(key, "BIST") == 0) {
                    cfg_bist_boot = parse_bool_val(val);
                } else if (strcasecmp(key, "SAVER") == 0 || strcasecmp(key, "SCREENSAVER") == 0) {
                    int m = 0;
                    for (int i = 0; val[i] >= '0' && val[i] <= '9'; i++) {
                        m = m * 10 + (val[i] - '0');
                    }
                    if (m >= 0 && m <= 120) cfg_saver_min = m;
                } else if (strcasecmp(key, "MEM_MODE") == 0 || strcasecmp(key, "MEM") == 0) {
                    if (strcasecmp(val, "256K") == 0 || strcasecmp(val, "256KB") == 0 || strcmp(val, "256") == 0) {
                        current_mem_mode = MEM_256K;
                    } else if (strcasecmp(val, "512K") == 0 || strcasecmp(val, "512KB") == 0 || strcmp(val, "512") == 0) {
                        current_mem_mode = MEM_512K;
                    } else {
                        current_mem_mode = MEM_AUTO;
                    }
                }
            }
        }
        line = next_line;
    }
}

static bool create_config_ini_on_sd() {
    if (!sd_card_ready || fat_lba == 0 || fat_size == 0 || spc == 0) return false;

    uint8_t dir_sec[512];
    uint32_t found_dir_lba = 0;
    int      found_dir_off = -1;

    uint32_t cl = root_clus;
    while (cl >= 2 && cl < 0x0FFFFFF8UL && found_dir_off < 0) {
        uint32_t base_lba = clus_to_lba(cl);
        for (uint32_t s = 0; s < spc && found_dir_off < 0; s++) {
            if (!sd_read_sec(base_lba + s, dir_sec)) return false;
            for (int off = 0; off < 512; off += 32) {
                uint8_t b0 = dir_sec[off];
                if (b0 == 0x00 || b0 == 0xE5) {
                    found_dir_lba = base_lba + s;
                    found_dir_off = off;
                    if (b0 == 0x00 && (off + 32) < 512) {
                        dir_sec[off + 32] = 0x00;
                    }
                    break;
                }
            }
        }
        if (found_dir_off < 0) cl = next_clus(cl);
    }

    if (found_dir_off < 0) return false;

    uint8_t  fat_sec[512];
    uint32_t new_clus = 0;
    uint32_t max_fat_scan = (fat_size < 512UL) ? fat_size : 512UL;

    for (uint32_t s = 0; s < max_fat_scan && new_clus == 0; s++) {
        if (!sd_read_sec(fat_lba + s, fat_sec)) return false;
        for (int off = 0; off < 512; off += 4) {
            uint32_t c_idx = (s * 128UL) + (uint32_t)(off >> 2);
            if (c_idx < 2) continue;
            if ((u32_le(&fat_sec[off]) & 0x0FFFFFFFUL) == 0) {
                new_clus = c_idx;
                wr32_le(&fat_sec[off], 0x0FFFFFFFUL);
                for (uint8_t f = 0; f < fat_count; f++) {
                    sd_write_sec(fat_lba + (uint32_t)f * fat_size + s, fat_sec);
                }
                break;
            }
        }
    }

    if (new_clus < 2) return false;

    uint8_t* entry = &dir_sec[found_dir_off];
    memset(entry, 0, 32);
    memcpy(entry, "CONFIG  INI", 11);
    entry[11] = 0x20;

    wr16_le(&entry[14], 0x6000);
    wr16_le(&entry[16], 0x5D52);
    wr16_le(&entry[18], 0x5D52);
    wr16_le(&entry[22], 0x6000);
    wr16_le(&entry[24], 0x5D52);

    wr16_le(&entry[20], (uint16_t)((new_clus >> 16) & 0xFFFF));
    wr16_le(&entry[26], (uint16_t)(new_clus & 0xFFFF));
    wr32_le(&entry[28], 256UL);

    if (!sd_write_sec(found_dir_lba, dir_sec)) return false;

    uint8_t zero_sec[512];
    memset(zero_sec, 0, sizeof(zero_sec));
    uint32_t cl_lba = clus_to_lba(new_clus);
    for (uint32_t s = 1; s < spc; s++) {
        sd_write_sec(cl_lba + s, zero_sec);
    }

    cfg_data_lba  = cl_lba;
    cfg_dir_lba   = found_dir_lba;
    cfg_dir_off   = found_dir_off;
    cfg_file_size = 256UL;
    return true;
}

bool save_config_ini(const char* new_rom_name, MemMode new_mode, bool force_write = false) {
    if (!sd_card_ready) return false;

    bool just_created = false;
    if (cfg_data_lba == 0 || cfg_dir_lba == 0) {
        if (!create_config_ini_on_sd()) {
            return false;
        }
        just_created = true;
    }

    const char* target_rom = (new_rom_name && new_rom_name[0] != '\0') ? new_rom_name : cfg_target_rom;

    if (!force_write && !just_created &&
        strcasecmp(cfg_target_rom, target_rom) == 0 &&
        current_mem_mode == new_mode) {
        return true;
    }

    if (target_rom != cfg_target_rom) {
        strncpy(cfg_target_rom, target_rom, sizeof(cfg_target_rom) - 1);
        cfg_target_rom[sizeof(cfg_target_rom) - 1] = '\0';
    }
    current_mem_mode = new_mode;

    const char* mode_str = "AUTO";
    if (current_mem_mode == MEM_256K)      mode_str = "256K";
    else if (current_mem_mode == MEM_512K) mode_str = "512K";

    uint8_t sec[512];
    memset(sec, 0x00, sizeof(sec));

    int written = snprintf((char*)sec, 254,
        "# ALF FLASH CARTRIDGE CONFIG\r\n"
        "ROM=%s\r\n"
        "AUTO_MOUNT=%d\r\n"
        "INTRO=%d\r\n"
        "SAVER=%d\r\n"
        "SHOW_TESTS=%d\r\n"
        "BIST_BOOT=%d\r\n"
        "MEM_MODE=%s\r\n",
        (cfg_target_rom[0] != '\0') ? cfg_target_rom : "game.bin",
        cfg_autostart  ? 1 : 0,
        cfg_play_intro ? 1 : 0,
        cfg_saver_min,
        cfg_show_tests ? 1 : 0,
        cfg_bist_boot  ? 1 : 0,
        mode_str
    );

    if (written < 0) written = 0;
    if (written > 254) written = 254;

    for (int i = written; i < 254; i++) {
        sec[i] = ' ';
    }
    sec[254] = '\r';
    sec[255] = '\n';

    if (!sd_write_sec(cfg_data_lba, sec)) {
        return false;
    }

    if (cfg_file_size != 256UL) {
        uint8_t dir_sec[512];
        if (sd_read_sec(cfg_dir_lba, dir_sec)) {
            wr32_le(&dir_sec[cfg_dir_off + 28], 256UL);
            if (sd_write_sec(cfg_dir_lba, dir_sec)) {
                cfg_file_size = 256UL;
            }
        }
    }

    return true;
}

// Сканирование FAT32 с гарантированным выбором рабочего образа для автомонтирования
bool scan_fat32_catalog(int& cursor_out) {
    current_mem_mode  = MEM_AUTO;
    cfg_play_intro    = false;
    cfg_show_tests    = true;
    cfg_bist_boot     = false;
    cfg_autostart     = true;  // По умолчанию автомонтирование при старте включено!
    cfg_saver_min     = 3;
    cfg_target_rom[0] = '\0';
    cfg_data_lba      = 0;
    cfg_dir_lba       = 0;
    cfg_dir_off       = 0;
    cfg_file_size     = 0;
    cursor_out        = 0;

    if (!sd_init_spi()) return false;

    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));
    if (!sd_read_sec(0, sec)) {
        sd_card_ready = false;
        return false;
    }

    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        sd_card_ready = false;
        return false;
    }

    uint32_t vbr = 0;
    if (sec[0x52] != 'F' && sec[0x36] != 'F') {
        vbr = u32_le(&sec[0x1C6]);
        if (vbr == 0 || !sd_read_sec(vbr, sec)) {
            sd_card_ready = false;
            return false;
        }
        if (sec[510] != 0x55 || sec[511] != 0xAA) {
            sd_card_ready = false;
            return false;
        }
    }

    uint16_t rsvd = u16_le(&sec[0x0E]);
    fat_count     = sec[0x10];
    fat_size      = u16_le(&sec[0x16]);
    if (fat_size == 0) fat_size = u32_le(&sec[0x24]);

    spc       = sec[0x0D];
    root_clus = u32_le(&sec[0x2C]);

    if (spc == 0 || spc > 128 || rsvd == 0 || fat_count == 0 || fat_size == 0 || root_clus < 2) {
        sd_card_ready = false;
        return false;
    }

    fat_lba  = vbr + rsvd;
    data_lba = fat_lba + ((uint32_t)fat_count * fat_size);

    cfg_play_intro = true;

    uint32_t cfg_clus = 0;
    uint32_t cl = root_clus;

    while (cl >= 2 && cl < 0x0FFFFFF8UL && cfg_clus == 0) {
        uint32_t lba = clus_to_lba(cl);
        for (uint32_t s = 0; s < spc && cfg_clus == 0; s++) {
            if (!sd_read_sec(lba + s, sec)) break;
            for (int off = 0; off < 512; off += 32) {
                uint8_t b0 = sec[off];
                if (b0 == 0x00) break;
                if (b0 == 0xE5 || (sec[off + 11] & 0x18) || sec[off + 11] == 0x0F) continue;

                char short_name[13];
                int p = 0;
                for (int i = 0; i < 8 && sec[off + i] != ' '; i++) short_name[p++] = sec[off + i];
                short_name[p++] = '.';
                for (int i = 8; i < 11 && sec[off + i] != ' '; i++) short_name[p++] = sec[off + i];
                short_name[p] = '\0';

                if (strcasecmp(short_name, "CONFIG.INI") == 0 ||
                    strcasecmp(short_name, "CONFIG.TXT") == 0) {
                    uint32_t hi = u16_le(&sec[off + 20]);
                    uint32_t lo = u16_le(&sec[off + 26]);
                    cfg_clus      = (hi << 16) | lo;
                    cfg_file_size = u32_le(&sec[off + 28]);
                    cfg_dir_lba   = lba + s;
                    cfg_dir_off   = off;
                    break;
                }
            }
        }
        cl = next_clus(cl);
    }

    if (cfg_clus >= 2 && cfg_file_size > 0) {
        cfg_data_lba = clus_to_lba(cfg_clus);
        char cfg_buf[513];
        memset(cfg_buf, 0, sizeof(cfg_buf));
        if (sd_read_sec(cfg_data_lba, (uint8_t*)cfg_buf)) {
            if (cfg_file_size < 512) cfg_buf[cfg_file_size] = '\0';
            else                     cfg_buf[512] = '\0';
            parse_config_text(cfg_buf);
        }
    }

    item_count = 0;
    if (cfg_show_tests) {
        items[0].type = ITEM_HW_TEST;
        strcpy(items[0].title, "[ RUN HW TEST ]");
        items[0].raw_name[0] = '\0';

        items[1].type = ITEM_MEM_MODE;
        items[1].raw_name[0] = '\0';

        items[2].type = ITEM_AUTOMOUNT;
        items[2].raw_name[0] = '\0';

        items[3].type = ITEM_SAVER;
        strcpy(items[3].title, "[ ALF SCREENSAVER ]");
        items[3].raw_name[0] = '\0';

        item_count = 4;
        update_service_menu_titles();
    }

    char lfn_temp[64];
    memset(lfn_temp, 0, sizeof(lfn_temp));
    bool has_lfn = false;

    cl = root_clus;
    while (cl >= 2 && cl < 0x0FFFFFF8UL && item_count < MAX_ITEMS) {
        uint32_t lba = clus_to_lba(cl);
        for (uint32_t s = 0; s < spc && item_count < MAX_ITEMS; s++) {
            if (!sd_read_sec(lba + s, sec)) break;
            for (int off = 0; off < 512; off += 32) {
                uint8_t b0 = sec[off];
                if (b0 == 0x00) goto scan_done;
                if (b0 == 0xE5) { has_lfn = false; continue; }

                uint8_t attr = sec[off + 11];
                if (attr == 0x0F) {
                    if (b0 & 0x40) memset(lfn_temp, 0, sizeof(lfn_temp));
                    extract_lfn_part(&sec[off], lfn_temp);
                    has_lfn = true;
                    continue;
                }

                if (attr & 0x18) { has_lfn = false; continue; }

                const char* ext = (const char*)&sec[off + 8];
                if (strncasecmp(ext, "BIN", 3) != 0 && strncasecmp(ext, "ROM", 3) != 0) {
                    has_lfn = false;
                    continue;
                }

                uint32_t hi    = u16_le(&sec[off + 20]);
                uint32_t lo    = u16_le(&sec[off + 26]);
                uint32_t fsize = u32_le(&sec[off + 28]);

                items[item_count].type    = ITEM_ROM;
                items[item_count].cluster = (hi << 16) | lo;
                items[item_count].size    = fsize;

                char raw_name[64];
                if (has_lfn && lfn_temp[0] != '\0') {
                    lfn_temp[63] = '\0';
                    strcpy(raw_name, lfn_temp);
                } else {
                    int p = 0;
                    for (int i = 0; i < 8 && sec[off + i] != ' '; i++) raw_name[p++] = sec[off + i];
                    raw_name[p++] = '.';
                    for (int i = 8; i < 11 && sec[off + i] != ' '; i++) raw_name[p++] = sec[off + i];
                    raw_name[p] = '\0';
                }
                has_lfn = false;

                strncpy(items[item_count].raw_name, raw_name, sizeof(items[item_count].raw_name) - 1);
                items[item_count].raw_name[sizeof(items[item_count].raw_name) - 1] = '\0';

                char size_tag[12];
                if (fsize > 524288UL) {
                    strcpy(size_tag, "[1M!] ");
                } else {
                    uint32_t kb = (fsize + 512UL) / 1024UL;
                    if (kb == 0 && fsize > 0) kb = 1;
                    snprintf(size_tag, sizeof(size_tag), "[%luK] ", (unsigned long)kb);
                }

                snprintf(items[item_count].title, sizeof(items[item_count].title), "%s%s", size_tag, raw_name);
                item_count++;
                if (item_count >= MAX_ITEMS) break;
            }
        }
        cl = next_clus(cl);
    }

scan_done:
    if (item_count == 0) {
        items[0].type = ITEM_INFO;
        strcpy(items[0].title, "NO ROM FILES");
        item_count = 1;
    }

    // 1. По умолчанию выбираем первый валидный образ ПЗУ (до 512 КБ)
    int first_rom_idx = -1;
    for (int i = 0; i < item_count; i++) {
        if (items[i].type == ITEM_ROM && items[i].size > 0 && items[i].size <= 524288UL) {
            first_rom_idx = i;
            break;
        }
    }
    if (first_rom_idx < 0) {
        for (int i = 0; i < item_count; i++) {
            if (items[i].type == ITEM_ROM) {
                first_rom_idx = i;
                break;
            }
        }
    }

    if (first_rom_idx >= 0) {
        cursor_out = first_rom_idx;
    }

    // 2. Если файла config.ini не было на флешке — создаем его с именем первой найденной игры
    if (cfg_data_lba == 0) {
        const char* first_rom = (first_rom_idx >= 0) ? items[first_rom_idx].raw_name : "game.bin";
        save_config_ini(first_rom, current_mem_mode, true);
    }

    // 3. Если в config.ini задан конкретный образ — переключаем курсор на него
    if (cfg_target_rom[0] != '\0') {
        for (int i = 0; i < item_count; i++) {
            if (items[i].type == ITEM_ROM &&
                (strcasecmp(items[i].raw_name, cfg_target_rom) == 0 ||
                 strcasecmp(items[i].title, cfg_target_rom) == 0)) {
                cursor_out = i;
                break;
            }
        }
    }

    return true;
}

bool load_selected_game(const FileItem& game) {
    if (!sd_check_card_alive() || game.cluster < 2 || spc == 0) {
        oled_clear();
        oled_print(0, 2,  "NO SD CARD!");
        oled_print(0, 12, "INSERT MICROSD");
        oled_print(0, 22, "SLOT IS EMPTY");
        oled_draw_alf32(96, 0, 2);
        oled_flush();
        sleep_ms(1500);
        return false;
    }

    if (game.size > 524288UL || game.size == 0) {
        oled_clear();
        oled_print(0, 2,  "ERROR: 1MB ROM");
        oled_print(0, 12, "NOT SUPPORTED!");
        oled_print(0, 22, "MAX SRAM: 512KB");
        oled_draw_alf32(96, 0, 2);
        oled_flush();
        sleep_ms(2500);
        return false;
    }

    gpio_put(PIN_Z80_RESET, 1);
    gpio_put(PIN_MCU_ACTIVE, 1);
    sleep_ms(5);

    bool use_256k;
    if (current_mem_mode == MEM_256K)      use_256k = true;
    else if (current_mem_mode == MEM_512K) use_256k = false;
    else                                   use_256k = (game.size <= 262144UL);

    cpld_set_256k_mode(use_256k);

    uint8_t sec[512];
    memset(sec, 0xFF, sizeof(sec));
    for (uint32_t b = 0; b < 32; b++) {
        sram_write_buf(b * 0x4000UL, sec, 512);
    }

    uint32_t cl  = game.cluster;
    uint32_t pos = 0;

    while (cl >= 2 && cl < 0x0FFFFFF8UL && pos < game.size) {
        uint32_t lba = clus_to_lba(cl);
        for (uint32_t s = 0; s < spc && pos < game.size; s++) {
            if (!sd_read_sec(lba + s, sec)) {
                sd_card_ready = false;
                oled_clear();
                oled_print(0, 4,  "SD REMOVED!");
                oled_print(0, 16, "LOAD ABORTED");
                oled_draw_alf32(96, 0, 2);
                oled_flush();
                sleep_ms(1500);
                return false;
            }

            if (pos < 0x80000UL) {
                sram_write_buf(pos, sec, 512);
            }

            pos += 512;
            if ((pos & 0x3FFF) == 0) {
                int anim_f = ((pos >> 14) & 1) ? 4 : 0;
                oled_clear();
                oled_print(0, 2, use_256k ? "MOUNT [256K ROM]" : "MOUNT [512K ROM]");
                oled_progress(14, (pos * 100UL) / game.size);
                oled_print(0, 25, "ALF: NO PROBLEM!");
                oled_draw_alf32(96, 0, anim_f);
                oled_flush();
            }
        }
        cl = next_clus(cl);
    }

    gpio_put(PIN_MCU_ACTIVE, 0);
    sleep_ms(10);
    gpio_put(PIN_Z80_RESET, 0);
    return true;
}

// =============================================================================
// 7. КНОПКИ SW1..SW3, АВТОМОНТИРОВАНИЕ ПРИ СТАРТЕ, СКРИНСЕЙВЕР И ГЛАВНЫЙ ЦИКЛ
// =============================================================================
enum KeyEv { KEY_NONE, KEY_UP, KEY_DOWN, KEY_OK_SHORT, KEY_OK_LONG, KEY_SAVER_COMBO };

void keys_init() {
    gpio_init(PIN_BTN_UP);     gpio_set_dir(PIN_BTN_UP, GPIO_IN);
    gpio_init(PIN_BTN_DOWN);   gpio_set_dir(PIN_BTN_DOWN, GPIO_IN);
    gpio_init(PIN_BTN_OK);     gpio_set_dir(PIN_BTN_OK, GPIO_IN);
    gpio_init(PIN_Z80_RST_IN); gpio_set_dir(PIN_Z80_RST_IN, GPIO_IN);
}

static bool any_button_held() {
    return (!gpio_get(PIN_BTN_UP) || !gpio_get(PIN_BTN_DOWN) || !gpio_get(PIN_BTN_OK));
}

void play_alf_boot_cartoon() {
    for (int step = 0; step < 36; step++) {
        if (any_button_held()) return;

        int cat_x = 12 + step * 4;
        int alf_x = -30 + step * 3;
        if (alf_x > 96) alf_x = 96;

        int chase_frame = (step & 2) ? 4 : ((step & 1) ? 3 : 0);

        oled_clear();
        oled_print(0, 2, "HERE KITTY-KITTY!");
        if (cat_x < 128) {
            oled_draw_icon16(cat_x, 14 + (step & 1), sprite_running_cat[step & 1], false);
        }
        oled_draw_alf32(alf_x, 0, chase_frame, false, false);
        oled_flush();
        sleep_ms(40);
    }

    const uint8_t laugh_seq[8] = { 4, 5, 4, 5, 3, 3, 0, 0 };
    for (int i = 0; i < 8; i++) {
        if (any_button_held()) return;

        oled_clear();
        oled_print(0, 4,  "CAT IS GONE! HA!");
        oled_print(0, 14, "ALF: NO PROBLEM!");
        oled_print(0, 24, "MELMAC CARTRIDGE");
        oled_draw_alf32(96, 0, laugh_seq[i], true, false);
        oled_flush();
        sleep_ms(130);
    }
}

KeyEv poll_keys() {
    static bool pu = true, pd = true, po = true, hold_trig = false, combo_trig = false;
    static uint32_t t_ok = 0;

    bool cu = gpio_get(PIN_BTN_UP);
    bool cd = gpio_get(PIN_BTN_DOWN);
    bool co = gpio_get(PIN_BTN_OK);
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (!cu && !cd) {
        KeyEv ev = combo_trig ? KEY_NONE : KEY_SAVER_COMBO;
        combo_trig = true;
        pu = cu; pd = cd; po = co;
        return ev;
    }
    if (cu && cd) {
        combo_trig = false;
    }

    KeyEv ev = KEY_NONE;
    if (!combo_trig) {
        if (!cu && pu) ev = KEY_UP;
        if (!cd && pd) ev = KEY_DOWN;
    }

    if (!co && po) {
        t_ok = now;
        hold_trig = false;
    } else if (!co && !po && !hold_trig && (now - t_ok > 1000)) {
        hold_trig = true;
        ev = KEY_OK_LONG;
    } else if (co && !po && !hold_trig && (now - t_ok > 25)) {
        ev = KEY_OK_SHORT;
    }

    pu = cu; pd = cd; po = co;
    return ev;
}

static void setup_no_sd_fallback_menu() {
    items[0].type = ITEM_HW_TEST;
    strcpy(items[0].title, "[ RUN HW TEST ]");
    items[1].type = ITEM_SAVER;
    strcpy(items[1].title, "[ ALF SCREENSAVER ]");
    item_count = 2;
}

int main() {
    stdio_init_all();
    cpld_bridge_init();
    keys_init();
    oled_init();

    int  cursor        = 0;
    bool sd_ok         = scan_fat32_catalog(cursor);
    bool no_sd_service = false;
    char status_msg[32];

    // 1. Если карта установлена при включении питания — показываем мультфильм и (опционально) BIST
    if (sd_ok && cfg_play_intro && !any_button_held()) {
        play_alf_boot_cartoon();
    }

    if (sd_ok && cfg_bist_boot) {
        bool ok = run_hardware_bist(status_msg, sizeof(status_msg));
        oled_clear();
        oled_print(0, 4,  ok ? "BIST PASSED!" : "BIST FAILED!");
        oled_print(0, 14, status_msg);
        oled_print(0, 24, ok ? "ALF: NO PROBLEM!" : "ALF: OH WILLY!");
        oled_draw_alf32(96, 0, ok ? 3 : 2);
        oled_flush();
        sleep_ms(ok ? 800 : 4000);
    }

    bool     in_game         = false;
    bool     saver_active    = false;
    int      quote_idx       = 0;
    uint32_t laugh_until     = 0;
    uint32_t select_t0       = to_ms_since_boot(get_absolute_time());
    uint32_t last_key_t0     = select_t0;
    uint32_t saver_start_t0  = select_t0;
    uint32_t last_sd_poll_t0 = select_t0;

    // 2. АВТОМОНТИРОВАНИЕ ПРИ ВКЛЮЧЕНИИ С УСТАНОВЛЕННОЙ КАРТОЙ ПО УМОЛЧАНИЮ
    // (Если не зажата кнопка на картридже, сразу загружаем выбранный образ в SRAM U6)
    if (sd_ok && cfg_autostart && !any_button_held() &&
        cursor >= 0 && cursor < item_count && items[cursor].type == ITEM_ROM) {
        if (load_selected_game(items[cursor])) {
            save_config_ini(items[cursor].raw_name, current_mem_mode, true);
            in_game         = true;
            select_t0       = to_ms_since_boot(get_absolute_time());
            last_key_t0     = select_t0;
            last_sd_poll_t0 = select_t0;
            laugh_until     = select_t0 + 800;
        }
    }

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        KeyEv ev = poll_keys();

        if (ev != KEY_NONE) {
            last_key_t0 = now;
            if (saver_active) {
                saver_active = false;
                oled_set_contrast(0x8F);
                select_t0    = now;
                laugh_until  = now + 600;
                if (!(in_game && ev == KEY_OK_LONG)) {
                    ev = KEY_NONE;
                }
            } else if (ev == KEY_SAVER_COMBO) {
                saver_active   = true;
                saver_start_t0 = now;
                oled_set_contrast(0x0A);
                ev = KEY_NONE;
            }
        }

        // =====================================================================
        // АВТООПРЕДЕЛЕНИЕ КАРТЫ И АВТОМАТИЧЕСКОЕ МОНТИРОВАНИЕ ОБРАЗА В ПАМЯТЬ
        // =====================================================================
        if (!sd_ok) {
            // Слот пуст: каждые 400 мс проверяем, не вставил ли пользователь microSD
            if (now - last_sd_poll_t0 >= 400) {
                last_sd_poll_t0 = now;
                if (sd_fast_detect_insertion()) {
                    if (saver_active) {
                        saver_active = false;
                        oled_set_contrast(0x8F);
                    }
                    oled_clear();
                    oled_print(0, 6,  "SD DETECTED!");
                    oled_print(0, 18, "MOUNTING ROM...");
                    oled_draw_alf32(96, 0, 3, true, false);
                    oled_flush();

                    if (scan_fat32_catalog(cursor)) {
                        sd_ok         = true;
                        no_sd_service = false;

                        if (!any_button_held() && cursor >= 0 && cursor < item_count &&
                            items[cursor].type == ITEM_ROM) {
                            if (load_selected_game(items[cursor])) {
                                save_config_ini(items[cursor].raw_name, current_mem_mode);
                                in_game = true;
                            }
                        }

                        now             = to_ms_since_boot(get_absolute_time());
                        select_t0       = now;
                        last_key_t0     = now;
                        last_sd_poll_t0 = now;
                        laugh_until     = now + 800;
                    }
                }
            }
        } else {
            // Карта установлена: каждые 500 мс проверяем, не извлекли ли её из слота
            if (now - last_sd_poll_t0 >= 500) {
                last_sd_poll_t0 = now;
                if (!sd_fast_check_present()) {
                    sd_ok         = false;
                    in_game       = false;
                    no_sd_service = false;
                    cursor        = 0;
                    gpio_put(PIN_Z80_RESET, 1);
                    gpio_put(PIN_MCU_ACTIVE, 1);
                    if (saver_active) {
                        saver_active = false;
                        oled_set_contrast(0x8F);
                    }
                    select_t0   = now;
                    last_key_t0 = now;
                }
            }
        }

        // Автоматический запуск скринсейвера по таймеру (cfg_saver_min минут)
        uint32_t saver_timeout_ms = (uint32_t)cfg_saver_min * 60000UL;
        if (!saver_active && cfg_saver_min > 0 && (now - last_key_t0 >= saver_timeout_ms)) {
            saver_active   = true;
            saver_start_t0 = now;
            oled_set_contrast(0x0A);
        }

        int timeline_step = (now / 220) & 0x0F;
        int alf_frame     = alf_timeline[timeline_step];
        if (now < laugh_until) {
            alf_frame = ((now / 120) & 1) ? 4 : 5;
        }

        if (saver_active) {
            render_alf_screensaver(now - saver_start_t0, alf_frame);
            sleep_ms(20);
            continue;
        }

        // =====================================================================
        // РЕЖИМ 1: КАРТА ОТСУТСТВУЕТ (ЭКРАН ОШИБКИ И ОЖИДАНИЯ "NO SD CARD!")
        // =====================================================================
        if (!sd_ok && !no_sd_service) {
            if (ev == KEY_DOWN || ev == KEY_UP) {
                setup_no_sd_fallback_menu();
                no_sd_service = true;
                cursor        = 0;
                select_t0     = now;
            }

            oled_clear();
            oled_print(0, 2,  "NO SD CARD!");
            oled_print(0, 12, "INSERT MICROSD");
            oled_print(0, 24, "TO MOUNT ROM...");
            oled_draw_alf32(96, 0, ((now / 400) & 1) ? 2 : 0, true, false);
            oled_flush();
            sleep_ms(20);
            continue;
        }

        // =====================================================================
        // РЕЖИМ 2: МЕНЮ ВЫБОРА ИГР И СЕРВИСНЫХ ФУНКЦИЙ
        // =====================================================================
        if (!in_game) {
            if (ev == KEY_UP) {
                if (cursor > 0) {
                    cursor--;
                    select_t0 = now;
                } else if (no_sd_service) {
                    no_sd_service = false;
                }
            } else if (ev == KEY_DOWN && cursor < item_count - 1) {
                cursor++;
                select_t0 = now;
            } else if (ev == KEY_OK_LONG) {
                sd_ok         = scan_fat32_catalog(cursor);
                no_sd_service = false;
                laugh_until   = now + 700;
                select_t0     = now;
                last_key_t0   = now;
            } else if (ev == KEY_OK_SHORT) {
                if (items[cursor].type == ITEM_HW_TEST) {
                    run_hardware_bist(status_msg, sizeof(status_msg));
                    oled_clear();
                    oled_print(0, 10, status_msg);
                    oled_draw_alf32(96, 0, 3);
                    oled_flush();
                    sleep_ms(2000);
                    last_key_t0     = to_ms_since_boot(get_absolute_time());
                    last_sd_poll_t0 = last_key_t0;
                } else if (items[cursor].type == ITEM_MEM_MODE) {
                    MemMode next_mode = MEM_AUTO;
                    if (current_mem_mode == MEM_AUTO)      next_mode = MEM_256K;
                    else if (current_mem_mode == MEM_256K) next_mode = MEM_512K;
                    else                                   next_mode = MEM_AUTO;

                    save_config_ini(cfg_target_rom, next_mode, true);
                    current_mem_mode = next_mode;
                    update_service_menu_titles();
                    laugh_until = now + 500;
                } else if (items[cursor].type == ITEM_AUTOMOUNT) {
                    cfg_autostart = !cfg_autostart;
                    update_service_menu_titles();
                    save_config_ini(cfg_target_rom, current_mem_mode, true);
                    laugh_until = now + 500;
                } else if (items[cursor].type == ITEM_SAVER) {
                    saver_active   = true;
                    saver_start_t0 = now;
                    oled_set_contrast(0x0A);
                } else if (items[cursor].type == ITEM_ROM) {
                    if (load_selected_game(items[cursor])) {
                        save_config_ini(items[cursor].raw_name, current_mem_mode);
                        in_game         = true;
                        laugh_until     = now + 900;
                        select_t0       = to_ms_since_boot(get_absolute_time());
                        last_key_t0     = select_t0;
                        last_sd_poll_t0 = select_t0;
                    } else if (!sd_card_ready) {
                        sd_ok         = false;
                        no_sd_service = false;
                        cursor        = 0;
                    }
                }
            }

            oled_clear();
            int page = (cursor / 4) * 4;
            for (int row = 0; row < 4; row++) {
                int idx = page + row;
                if (idx >= item_count) break;
                bool is_sel = (idx == cursor);
                if (is_sel) oled_print(0, row * 8, ">");
                oled_print_marquee(6, 92, row * 8, items[idx].title, is_sel, now - select_t0);
            }

            oled_draw_alf32(96, 0, alf_frame, true, false);
            oled_flush();

        // =====================================================================
        // РЕЖИМ 3: ОБРАЗ ПРИМОНТИРОВАН, ИГРА ЗАПУЩЕНА НА ПРИСТАВКЕ «ЭЛЬФ»
        // =====================================================================
        } else {
            if (ev == KEY_UP) {
                quote_idx   = (quote_idx + 1) % 6;
                laugh_until = now + 800;
            } else if (ev == KEY_DOWN) {
                quote_idx   = (quote_idx + 5) % 6;
                laugh_until = now + 800;
            } else if (ev == KEY_OK_SHORT) {
                saver_active   = true;
                saver_start_t0 = now;
                oled_set_contrast(0x0A);
                continue;
            }

            const char* top_line = (alf_frame >= 4) ? alf_quotes[1] : alf_quotes[quote_idx];

            oled_clear();
            oled_print_clipped(0, 2, 0, 92, top_line);
            oled_print_marquee(0, 92, 12, items[cursor].title, true, now - select_t0);
            oled_print_clipped(0, 24, 0, 92, "OK:SAVER HOLD:MENU");

            oled_draw_alf32(96, 0, alf_frame, true, false);
            oled_flush();

            if (ev == KEY_OK_LONG) {
                gpio_put(PIN_Z80_RESET, 1);
                gpio_put(PIN_MCU_ACTIVE, 1);
                in_game       = false;
                no_sd_service = false;
                select_t0     = now;
                last_key_t0   = now;
            }
        }

        sleep_ms(20);
    }
}
// ============================================================================
// alf_cartridge.c — прошивка Raspberry Pi Pico (RP2040) для универсального
// картриджа «Эльф» (проект alf-scar).
//
// Роли Pico:
//   * держит «Эльф» в RESET во время выбора/загрузки образа
//   * читает SD-карту (SPI1), список *.bin на OLED (I2C0)
//   * навигация кнопками UP/OK/DOWN
//   * грузит выбранный образ в SRAM через CPLD (SPI0)
//   * переключает CPLD в режим «картридж» и снимает RESET
// ============================================================================

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "ff.h"                 // FatFs
#include "diskio.h"             // low-level disk I/O (SD over SPI)
#include "sd_card.h"            // pico_sd_card: sd_card_t / sd_init_driver
#include "ssd1306.h"

// ---------------------------------------------------------------------------
// Пины (по нетлисту alf-scar)
// ---------------------------------------------------------------------------
#define BTN_UP      12
#define BTN_OK      7
#define BTN_DOWN    3
#define Z80_RESET   9       // HIGH = держим «Эльф» в сбросе (Q1 -> /RST=0)
#define RST_MON     20      // вход-монитор /RST (через U4.Y6)

#define SD_SCK      10
#define SD_MOSI     11      // = SD CMD
#define SD_MISO     8       // = SD DAT0
#define SD_CS       14      // = SD DAT3

#define CPLD_SCK    18
#define CPLD_MOSI   19
#define CPLD_MISO   16
#define CPLD_CS     17

#define CPLD_CLK    21      // такт 50 МГц для CPLD (clk_gpout)
#define MCU_ACTIVE  22      // HIGH = CPLD в режиме загрузки

#define I2C_SDA     4
#define I2C_SCL     5

#define SPI_CPLD    spi0
#define SPI_SD      spi1
#define I2C_OLED    i2c0

// ---------------------------------------------------------------------------
#define MAX_FILES   64
#define BUF_SIZE    4096

static char g_files[MAX_FILES][13];   // имена 8.3 (без LFN)
static int  g_count = 0;

// ---------------------------------------------------------------------------
static void init_clk_50mhz(void) {
    // Системную частоту ставим 100 МГц, на GPIO21 выводим clk_gpout / 2 = 50 МГц
    set_sys_clock_khz(100000, true);
    clock_gpio_init(CPLD_CLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 2);
}

static void init_cpld(void) {
    gpio_init(CPLD_CS);
    gpio_set_dir(CPLD_CS, GPIO_OUT);
    gpio_put(CPLD_CS, 1);                 // CS неактивен

    gpio_init(MCU_ACTIVE);
    gpio_set_dir(MCU_ACTIVE, GPIO_OUT);
    gpio_put(MCU_ACTIVE, 1);              // стартуем в режиме загрузки

    spi_init(SPI_CPLD, 10 * 1000 * 1000); // 10 МГц SPI к CPLD
    gpio_set_function(CPLD_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(CPLD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(CPLD_MISO, GPIO_FUNC_SPI);
}

static void init_reset(void) {
    gpio_init(Z80_RESET);
    gpio_set_dir(Z80_RESET, GPIO_OUT);
    gpio_put(Z80_RESET, 1);               // держим RESET (Z80 в сбросе)

    gpio_init(RST_MON);
    gpio_set_dir(RST_MON, GPIO_IN);
    gpio_pull_up(RST_MON);
}

static void init_buttons(void) {
    const uint8_t pins[] = {BTN_UP, BTN_OK, BTN_DOWN};
    for (int i = 0; i < 3; i++) {
        uint8_t p = pins[i];
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_pull_up(p);
    }
}

static void init_oled(void) {
    i2c_init(I2C_OLED, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(I2C_OLED);
}

// ---------------------------------------------------------------------------
// SD-карта через pico_sd_card. Настраивается объектом sd_card_t.
// ---------------------------------------------------------------------------
static sd_card_t sd;

// Хуки для pico_sd_card (обязательны для библиотеки)
size_t sd_get_num(void) { return 1; }
sd_card_t *sd_get_by_num(size_t num) { return num == 0 ? &sd : NULL; }

static bool init_sd(void) {
    spi_init(SPI_SD, 12 * 1000 * 1000);  // 12 МГц (SD в SPI-режиме)
    gpio_set_function(SD_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_MISO, GPIO_FUNC_SPI);

    gpio_init(SD_CS);
    gpio_set_dir(SD_CS, GPIO_OUT);
    gpio_put(SD_CS, 1);

    sd = (sd_card_t){
        .pcName = "0:",
        .spi_if = {
            .inst = SPI_SD,
            .baud_rate = 12 * 1000 * 1000,
            .set_drive_strength = true,
            .ss_gpio = SD_CS,
        },
        .use_card_detect = false,
        .m_Status = STA_NOINIT,
    };
    if (!sd_init_driver()) return false;

    FATFS fs;
    FRESULT fr = f_mount(&fs, "0:", 1);
    return fr == FR_OK;
}

// ---------------------------------------------------------------------------
static int list_images(void) {
    DIR dj;
    FILINFO fi;
    g_count = 0;
    FRESULT fr = f_opendir(&dj, "/");
    if (fr != FR_OK) return 0;
    while (g_count < MAX_FILES) {
        fr = f_readdir(&dj, &fi);
        if (fr != FR_OK || fi.fname[0] == 0) break;
        if (fi.fattrib & AM_DIR) continue;
        // только *.bin / *.rom / *.elf
        const char *p = strrchr(fi.fname, '.');
        if (!p) continue;
        if (!strcasecmp(p, ".bin") || !strcasecmp(p, ".rom") || !strcasecmp(p, ".elf")) {
            strncpy(g_files[g_count], fi.fname, 12);
            g_files[g_count][12] = 0;
            g_count++;
        }
    }
    f_closedir(&dj);
    return g_count;
}

// ---------------------------------------------------------------------------
// Потоковая загрузка файла в SRAM через CPLD (SPI0)
// ---------------------------------------------------------------------------
static bool load_image(const char *path) {
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return false;

    gpio_put(CPLD_CS, 0);                 // начало транзакции
    sleep_us(20);

    // 3 байта стартового адреса (0) — адрес 19 бит в старших разрядах
    uint8_t addr[3] = {0, 0, 0};
    spi_write_blocking(SPI_CPLD, addr, 3);

    static uint8_t buf[BUF_SIZE];
    UINT br;
    while (1) {
        if (f_read(&f, buf, sizeof(buf), &br) != FR_OK) break;
        if (br == 0) break;
        spi_write_blocking(SPI_CPLD, buf, br);
    }

    sleep_us(20);
    gpio_put(CPLD_CS, 1);                 // конец транзакции
    f_close(&f);
    return true;
}

// ---------------------------------------------------------------------------
// Переключение в режим «картридж» и запуск «Эльфа»
// ---------------------------------------------------------------------------
static void run_cartridge(void) {
    sleep_ms(20);
    gpio_put(MCU_ACTIVE, 0);              // CPLD -> режим обычного картриджа
    sleep_ms(20);
    gpio_put(Z80_RESET, 0);               // снимаем RESET — «Эльф» стартует
}

// ---------------------------------------------------------------------------
// Меню на OLED
// ---------------------------------------------------------------------------
static void draw_menu(int sel, int top) {
    ssd1306_clear();
    for (int i = 0; i < 4 && (top + i) < g_count; i++) {
        int idx = top + i;
        char line[24];
        snprintf(line, sizeof(line), "%c %-19s", (idx == sel) ? '>' : ' ', g_files[idx]);
        ssd1306_draw_string(0, i * 8, line);
    }
    if (g_count == 0)
        ssd1306_draw_string(0, 8, "No images (*.bin)");
    ssd1306_update();
}

static bool btn_pressed(uint pin) {
    if (gpio_get(pin) == 0) {           // активный низкий уровень
        sleep_ms(20);                    // антидребезг
        if (gpio_get(pin) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
int main(void) {
    stdio_init_all();
    init_clk_50mhz();
    init_cpld();
    init_reset();
    init_buttons();
    init_oled();

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "ALF cartridge");
    ssd1306_update();
    sleep_ms(400);

    if (!init_sd()) {
        ssd1306_clear();
        ssd1306_draw_string(0, 0, "SD error");
        ssd1306_update();
        while (1) sleep_ms(100);
    }

    int count = list_images();
    int sel = 0, top = 0;

    for (;;) {
        draw_menu(sel, top);
        sleep_ms(80);

        if (count == 0) continue;

        if (btn_pressed(BTN_UP)) {
            sel--;
            if (sel < 0) sel = count - 1;
            if (sel < top) top = sel;
            if (sel >= top + 4) top = sel - 3;
        }
        if (btn_pressed(BTN_DOWN)) {
            sel++;
            if (sel >= count) sel = 0;
            if (sel < top) top = sel;
            if (sel >= top + 4) top = sel - 3;
        }
        if (btn_pressed(BTN_OK)) {
            ssd1306_clear();
            ssd1306_draw_string(0, 0, "Loading...");
            ssd1306_draw_string(0, 16, g_files[sel]);
            ssd1306_update();

            if (load_image(g_files[sel])) {
                run_cartridge();
                // здесь «Эльф» уже работает; Pico больше ничего не делает
                for (;;) sleep_ms(1000);
            } else {
                ssd1306_clear();
                ssd1306_draw_string(0, 0, "Load error");
                ssd1306_update();
                sleep_ms(1500);
            }
        }
    }
}

// =============================================================================
// Прошивка микроконтроллера RP2040 для проекта SCARRY v1 (alf-scar)
// ПЛИС: Altera MAX II EPM240T100C5N (связь по SPI0: команды 0x01, 0x02, 0x03)
// Дисплей: OLED SSD1306 128x32 (I2C0) с анимацией персонажа Альфа (32x32)
// Автор: BY san010101 (BASED ON zxbyte.org)
// =============================================================================
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"

// -----------------------------------------------------------------------------
// 1. НАЗНАЧЕНИЕ ВЫВОДОВ РАСПБЕРРИ ПИ ПИКО (A1) В СХЕМЕ SCARRY v1
// -----------------------------------------------------------------------------
#define PIN_BTN_DOWN    3
#define PIN_I2C_SDA     4
#define PIN_I2C_SCL     5
#define PIN_BTN_OK      7
#define PIN_SD_MISO     8
#define PIN_Z80_RESET   9
#define PIN_SD_SCK      10
#define PIN_SD_MOSI     11
#define PIN_BTN_UP      12
#define PIN_SD_CS       14

#define PIN_SPI_MISO    16
#define PIN_SPI_CS      17
#define PIN_SPI_SCK     18
#define PIN_SPI_MOSI    19
#define PIN_Z80_RST_IN  20
#define PIN_CLK_50MHZ   21
#define PIN_MCU_ACTIVE  22

// Команды SPI-моста ПЛИС EPM240T100C5N
#define CPLD_CMD_WRITE  0x01
#define CPLD_CMD_READ   0x02
#define CPLD_CMD_MODE   0x03

#define OLED_ADDR       0x3C

// -----------------------------------------------------------------------------
// 2. СПРАЙТЫ ПЕРСОНАЖА АЛЬФА (32x32 ПИКСЕЛЯ, 4 СТРАНИЦЫ ПО 32 БАЙТА)
// -----------------------------------------------------------------------------
static const uint8_t alf_sprite_idle[128] = {
    0x00,0x00,0xE0,0xF8,0xFC,0xFE,0x7E,0x3C,0x78,0xF0,0xE0,0xC0,0xC0,0xE0,0xF0,0xF8,
    0xF8,0xF0,0xE0,0xC0,0xC0,0xE0,0xF0,0x78,0x3C,0x7E,0xFE,0xFC,0xF8,0xE0,0x00,0x00,
    0x00,0x38,0x7F,0xFF,0xFF,0xFF,0xFC,0xF0,0xE7,0xCF,0x9F,0x3F,0x3F,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0x3F,0x3F,0x9F,0xCF,0xE7,0xF0,0xFC,0xFF,0xFF,0xFF,0x7F,0x38,0x00,
    0x00,0x00,0x00,0x03,0x0F,0x1F,0x3F,0x7E,0xFC,0xF9,0xF3,0xE7,0xCF,0x9F,0xBF,0xBF,
    0xBF,0xBF,0x9F,0xCF,0xE7,0xF3,0xF9,0xFC,0x7E,0x3F,0x1F,0x0F,0x03,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x03,0x07,0x0F,0x1E,0x1C,0x39,0x3B,
    0x3B,0x39,0x1C,0x1E,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static const uint8_t alf_sprite_blink[128] = {
    0x00,0x00,0xE0,0xF8,0xFC,0xFE,0x7E,0x3C,0x78,0xF0,0xE0,0xC0,0xC0,0xE0,0xF0,0xF8,
    0xF8,0xF0,0xE0,0xC0,0xC0,0xE0,0xF0,0x78,0x3C,0x7E,0xFE,0xFC,0xF8,0xE0,0x00,0x00,
    0x00,0x38,0x7F,0xFF,0xFF,0xFF,0xFC,0xF0,0xFF,0xDF,0xDF,0xDF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xDF,0xDF,0xDF,0xFF,0xF0,0xFC,0xFF,0xFF,0xFF,0x7F,0x38,0x00,
    0x00,0x00,0x00,0x03,0x0F,0x1F,0x3F,0x7E,0xFC,0xF9,0xF3,0xE7,0xCF,0x9F,0xBF,0xBF,
    0xBF,0xBF,0x9F,0xCF,0xE7,0xF3,0xF9,0xFC,0x7E,0x3F,0x1F,0x0F,0x03,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x03,0x07,0x0F,0x1E,0x1C,0x39,0x3B,
    0x3B,0x39,0x1C,0x1E,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// -----------------------------------------------------------------------------
// 3. ДРАЙВЕР OLED SSD1306 (128x32)
// -----------------------------------------------------------------------------
static uint8_t oled_buf[512];

static void oled_cmd(uint8_t c) {
    uint8_t pkt[2] = { 0x00, c };
    i2c_write_blocking(i2c0, OLED_ADDR, pkt, 2, false);
}

void oled_init() {
    sleep_ms(50);
    const uint8_t init_seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
        0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    for (size_t i = 0; i < sizeof(init_seq); i++) {
        oled_cmd(init_seq[i]);
    }
    memset(oled_buf, 0, sizeof(oled_buf));
}

void oled_flush() {
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127);
    oled_cmd(0x22); oled_cmd(0); oled_cmd(3);

    uint8_t pkt[17];
    pkt[0] = 0x40;
    for (int i = 0; i < 512; i += 16) {
        memcpy(&pkt[1], &oled_buf[i], 16);
        i2c_write_blocking(i2c0, OLED_ADDR, pkt, 17, false);
    }
}

void oled_draw_alf(bool blink) {
    const uint8_t* spr = blink ? alf_sprite_blink : alf_sprite_idle;
    for (int page = 0; page < 4; page++) {
        memcpy(&oled_buf[page * 128], &spr[page * 32], 32);
    }
}

// -----------------------------------------------------------------------------
// 4. ДРАЙВЕР SPI-МОСТА К ПЛИС EPM240T100C5N И ПАМЯТИ SRAM U6
// -----------------------------------------------------------------------------
void cpld_set_mcu_mode(bool active) {
    if (active) {
        gpio_put(PIN_Z80_RESET, 1);  // Удерживаем Z80 в сбросе через Q1 (BCR108)
        gpio_put(PIN_MCU_ACTIVE, 1); // Переводим EPM240 и буфер U5 в режим загрузки
        sleep_ms(2);
    } else {
        gpio_put(PIN_MCU_ACTIVE, 0); // Отдаем шину приставке «Эльф»
        sleep_ms(10);
        gpio_put(PIN_Z80_RESET, 0);  // Отпускаем сигнал /*RST/
    }
}

void cpld_set_mapper_mode(bool mode_256k) {
    uint8_t pkt[2] = { CPLD_CMD_MODE, (uint8_t)(mode_256k ? 0x01 : 0x00) };
    gpio_put(PIN_SPI_CS, 0);
    spi_write_blocking(spi0, pkt, 2);
    gpio_put(PIN_SPI_CS, 1);
}

void sram_write_buf(uint32_t addr, const uint8_t* data, size_t len) {
    uint8_t hdr[4] = {
        CPLD_CMD_WRITE,
        (uint8_t)((addr >> 16) & 0x07),
        (uint8_t)((addr >> 8)  & 0xFF),
        (uint8_t)(addr & 0xFF)
    };
    gpio_put(PIN_SPI_CS, 0);
    spi_write_blocking(spi0, hdr, 4);
    spi_write_blocking(spi0, data, len);
    gpio_put(PIN_SPI_CS, 1);
}

void sram_read_buf(uint32_t addr, uint8_t* data, size_t len) {
    uint8_t hdr[5] = {
        CPLD_CMD_READ,
        (uint8_t)((addr >> 16) & 0x07),
        (uint8_t)((addr >> 8)  & 0xFF),
        (uint8_t)(addr & 0xFF),
        0x00 // Dummy byte для загрузки первого байта SRAM в сдвиговый регистр ПЛИС
    };
    gpio_put(PIN_SPI_CS, 0);
    spi_write_blocking(spi0, hdr, 5);
    spi_read_blocking(spi0, 0x00, data, len);
    gpio_put(PIN_SPI_CS, 1);
}

// Встроенный самотест памяти BIST (проверка всех 32 банков по 16 КБ)
bool sram_run_bist() {
    cpld_set_mcu_mode(true);
    uint8_t tx[64], rx[64];
    for (uint32_t bank = 0; bank < 32; bank++) {
        uint32_t base = bank * 0x4000UL;
        for (int i = 0; i < 64; i++) {
            tx[i] = (uint8_t)((i * 37 + bank * 13) ^ 0xA5);
        }
        sram_write_buf(base, tx, 64);
        sram_read_buf(base, rx, 64);
        if (memcmp(tx, rx, 64) != 0) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// 5. ТОЧКА ВХОДА И ГЛАВНЫЙ ЦИКЛ МЕНЮ
// -----------------------------------------------------------------------------
int main() {
    // Настраиваем системную частоту 100 МГц, чтобы получить ровно 50 МГц на GPIO21 (делитель 2)
    set_sys_clock_khz(100000, true);
    stdio_init_all();

    // Инициализация выводов управления сбросом Z80 и режимом ПЛИС
    gpio_init(PIN_Z80_RESET);  gpio_set_dir(PIN_Z80_RESET, GPIO_OUT);  gpio_put(PIN_Z80_RESET, 1);
    gpio_init(PIN_MCU_ACTIVE); gpio_set_dir(PIN_MCU_ACTIVE, GPIO_OUT); gpio_put(PIN_MCU_ACTIVE, 1);

    // Кнопки управления
    gpio_init(PIN_BTN_UP);   gpio_set_dir(PIN_BTN_UP, GPIO_IN);
    gpio_init(PIN_BTN_OK);   gpio_set_dir(PIN_BTN_OK, GPIO_IN);
    gpio_init(PIN_BTN_DOWN); gpio_set_dir(PIN_BTN_DOWN, GPIO_IN);

    // Запуск аппаратного тактирования 50 МГц для ПЛИС EPM240T100C5N
    clock_gpio_init(PIN_CLK_50MHZ, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 2);

    // Инициализация I2C0 для OLED SSD1306 (400 кГц)
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    oled_init();

    // Инициализация SPI0 для связи с ПЛИС (20 МГц)
    spi_init(spi0, 20 * 1000 * 1000);
    gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_SPI_CS); gpio_set_dir(PIN_SPI_CS, GPIO_OUT); gpio_put(PIN_SPI_CS, 1);

    // Запуск BIST при старте
    bool bist_ok = sram_run_bist();
    (void)bist_ok;

    bool blink_state = false;
    while (true) {
        oled_draw_alf(blink_state);
        oled_flush();
        blink_state = !blink_state;
        sleep_ms(500);
    }
    return 0;
}

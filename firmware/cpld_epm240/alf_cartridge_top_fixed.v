// ============================================================================
// alf_cartridge_top.v — универсальный картридж «Эльф» (CPLD EPM240 + SRAM 512КБ)
//
// Протокол картриджа «Эльф» (подтверждён схемотехникой референс-картриджа
// на 74LS174 + 74LS138 + EPROM):
//   A0..A13  — адрес внутри банка 16КБ (от CPU)
//   D0..D7   — двунаправленная шина данных
//   /PBANK   — строб защёлки банка: консоль пульсирует им при OUT (#5F),A;
//              в этот момент на шине данных лежит номер банка (D0..D5).
//              Защёлкиваем по переднему (нарастающему) фронту, как 74LS174 по Cp.
//   /ROMC    — разрешение ПЗУ картриджа (активен, когда консоль читает ПЗУ картриджа)
//   /RST     — сброс (сбрасывает регистр банка)
//
// Режимы:
//   mcu_mode = 1 — Raspberry Pi Pico грузит образ в SRAM по SPI (шина Z80 отключена)
//   mcu_mode = 0 — игровой: эмулируем банковое ПЗУ картриджа
// ============================================================================
module alf_cartridge_top (
    input  wire        CLK_50MHZ,

    // Шина приставки «Эльф» (5В -> 3.3В через 74AHCT541/245 с VCC=3.3V)
    input  wire [13:0] Z80_A,        // A0..A13
    inout  wire [7:0]  D_33V,        // внутренняя (3.3В) сторона буфера 245
    input  wire        Z80_PBANK_N,  // строб защёлки банка
    input  wire        Z80_ROMC_N,   // разрешение ПЗУ картриджа
    input  wire        Z80_RST_N,    // сброс

    // Управление буфером 74AHCT245
    output wire        BUF_DIR,      // 0 = B->A (выдача в шину), 1 = A->B (приём из шины)
    output wire        BUF_OE_N,

    // SRAM 512КБ (AS6C4008, 19 адресных линий)
    output wire [18:0] SRAM_A,
    output wire        SRAM_CE_N,
    output wire        SRAM_OE_N,
    output wire        SRAM_WE_N,

    // Raspberry Pi Pico
    input  wire        MCU_ACTIVE,
    input  wire        SPI_CS_N,
    input  wire        SPI_SCK,
    input  wire        SPI_MOSI,
    output wire        SPI_MISO      // статусный выход (пока зарезервирован)
);

    // ==================== Режим загрузки от Pico ====================
    reg [1:0] s_m;
    always @(posedge CLK_50MHZ) s_m <= {s_m[0], MCU_ACTIVE};
    wire mcu_mode = s_m[1];

    // ==================== Регистр банка ПЗУ ====================
    reg [1:0] s_pb;
    always @(posedge CLK_50MHZ) s_pb <= {s_pb[0], Z80_PBANK_N};
    wire pb_rise = (s_pb == 2'b01);          // передний фронт /PBANK (как Cp у 74LS174)

    reg [5:0] bank;
    always @(posedge CLK_50MHZ) begin
        if (!Z80_RST_N)   bank <= 6'b000000;  // сброс регистра банка
        else if (pb_rise) bank <= D_33V[5:0]; // защёлкиваем номер банка (D0..D5)
    end

    // ==================== Чтение ПЗУ картриджа ====================
    wire rom_read   = ~Z80_ROMC_N;   // консоль разрешила чтение ПЗУ картриджа
    wire empty_bank = bank[5];       // банки 32..63 не помещаются в SRAM 512КБ

    // ==================== Буфер данных 74AHCT245 ====================
    wire bank_phase = ~Z80_PBANK_N;  // во время пульса /PBANK принимаем банк
    assign BUF_OE_N = mcu_mode ? 1'b1 : ~(rom_read | bank_phase);
    assign BUF_DIR  = rom_read ? 1'b0 : 1'b1;   // rom_read: B->A (выдача), иначе A->B (приём)

    // ==================== SRAM ====================
    assign SRAM_A    = mcu_mode ? mcu_addr : {bank[4:0], Z80_A[13:0]};
    assign SRAM_CE_N = mcu_mode ? spi_cs  : (rom_read & ~empty_bank) ? 1'b0 : 1'b1;
    assign SRAM_OE_N = mcu_mode ? 1'b1    : 1'b0;   // в игре SRAM только читаем
    assign SRAM_WE_N = mcu_mode ? ~mcu_sram_we : 1'b1;

    // ==================== Шина данных ====================
    // В mcu_mode CPLD выдаёт байт в SRAM при записи; в игре D_33V в Z (данные идут из SRAM).
    // Для пустых банков (32..63) отдаём #00 — сканер меню их корректно пропустит.
    assign D_33V = mcu_mode ? ((sram_we_state != 0) ? data_latch : 8'hZZ)
                            : ((rom_read & empty_bank) ? 8'h00 : 8'hZZ);

    // Статусный выход для Pico (пока не используется — отдаём признак режима)
    assign SPI_MISO = mcu_mode;

    // ==================== SPI-приёмник от Pico ====================
    // Протокол: Pico опускает CS, шлёт 24 бита стартового адреса (используются
    // младшие 19), затем байты данных с автоинкрементом адреса.
    reg [2:0] sync_spi_cs, sync_spi_sck;
    reg [1:0] sync_spi_mosi;
    always @(posedge CLK_50MHZ) begin
        sync_spi_cs   <= {sync_spi_cs[1:0], SPI_CS_N};
        sync_spi_sck  <= {sync_spi_sck[1:0], SPI_SCK};
        sync_spi_mosi <= {sync_spi_mosi[0], SPI_MOSI};
    end
    wire spi_cs       = sync_spi_cs[2];
    wire spi_sck_rise = (sync_spi_sck[2:1] == 2'b01);
    wire spi_mosi     = sync_spi_mosi[1];

    reg [18:0] mcu_addr;
    reg [7:0]  shift_reg;
    reg [7:0]  data_latch;
    reg [1:0]  byte_cnt;
    reg [2:0]  bit_cnt;
    reg        receiving_data;
    reg        mcu_write_req;

    always @(posedge CLK_50MHZ) begin
        if (spi_cs) begin
            byte_cnt       <= 0;
            bit_cnt        <= 0;
            receiving_data <= 0;
            mcu_write_req  <= 0;
        end else if (spi_sck_rise) begin
            bit_cnt <= bit_cnt + 1;
            if (!receiving_data) begin
                // первые 3 байта — стартовый адрес (накапливаем младшие 19 бит)
                mcu_addr <= {mcu_addr[17:0], spi_mosi};
                if (bit_cnt == 7) begin
                    if (byte_cnt == 2) receiving_data <= 1;
                    else                byte_cnt <= byte_cnt + 1;
                end
            end else begin
                shift_reg <= {shift_reg[6:0], spi_mosi};
                if (bit_cnt == 7) begin
                    data_latch    <= {shift_reg[6:0], spi_mosi};
                    mcu_write_req <= 1;
                end else begin
                    mcu_write_req <= 0;
                end
            end
        end else begin
            mcu_write_req <= 0;
        end
    end

    // Генератор импульса записи в SRAM (только в mcu_mode)
    // WE низкий 3 такта = 60 нс (tWP min для SRAM-55 = 50 нс)
    reg [2:0] sram_we_state;
    reg       mcu_sram_we;
    always @(posedge CLK_50MHZ) begin
        if (mcu_mode) begin
            if (mcu_write_req && sram_we_state == 0) begin
                mcu_sram_we   <= 1;
                sram_we_state <= 1;
            end else if (sram_we_state == 1) begin
                sram_we_state <= 2;
            end else if (sram_we_state == 2) begin
                sram_we_state <= 3;
            end else if (sram_we_state == 3) begin
                mcu_sram_we   <= 0;
                mcu_addr      <= mcu_addr + 1;
                sram_we_state <= 0;
            end
        end else begin
            sram_we_state <= 0;
            mcu_sram_we   <= 0;
        end
    end

endmodule

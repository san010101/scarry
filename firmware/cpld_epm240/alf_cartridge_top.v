module alf_cartridge_top (
    input  wire        CLK_50MHZ,

    // Интерфейс шины Z80 (ZX Spectrum)
    input  wire [15:0] Z80_A,
    inout  wire [7:0]  D_33V,     // Общая шина данных (двунаправленная)
    input  wire        Z80_MREQ_N,
    input  wire        Z80_RD_N,
    input  wire        Z80_WR_N,
    input  wire        Z80_RFSH_N,
    output wire        Z80_ROMCS_N,

    // Управление буферами (74LVC245)
    output wire        BUF_DIR,
    output wire        BUF_OE_N,

    // Интерфейс памяти SRAM (AS6C4008)
    output wire [18:0] SRAM_A,
    output wire        SRAM_CE_N,
    output wire        SRAM_OE_N,
    output wire        SRAM_WE_N,

    // Интерфейс микроконтроллера Raspberry Pi Pico
    input  wire        MCU_ACTIVE,
    input  wire        SPI_CS_N,
    input  wire        SPI_SCK,
    input  wire        SPI_MOSI
);

    // =========================================================================
    // 1. СИНХРОНИЗАЦИЯ ДОМЕНОВ (Защита от метастабильности)
    // =========================================================================
    // Все внешние сигналы от Pico должны быть привязаны к тактам 50 МГц
    
    reg [2:0] sync_mcu_active;
    reg [2:0] sync_spi_cs;
    reg [2:0] sync_spi_sck;
    reg [1:0] sync_spi_mosi;

    always @(posedge CLK_50MHZ) begin
        sync_mcu_active <= {sync_mcu_active[1:0], MCU_ACTIVE};
        sync_spi_cs     <= {sync_spi_cs[1:0], SPI_CS_N};
        sync_spi_sck    <= {sync_spi_sck[1:0], SPI_SCK};
        sync_spi_mosi   <= {sync_spi_mosi[0], SPI_MOSI};
    end

    wire mcu_mode     = sync_mcu_active[2];
    wire spi_cs       = sync_spi_cs[2];
    wire spi_sck_rise = (sync_spi_sck[2:1] == 2'b01); // Детектор фронта SCK
    wire spi_mosi     = sync_spi_mosi[1];

    // =========================================================================
    // 2. ПРИЕМНИК SPI ОТ PICO (Потоковая загрузка)
    // =========================================================================
    // Протокол: Pico опускает CS и отправляет 24 бита стартового адреса. 
    // Затем отправляет байты данных один за другим. Адрес увеличивается сам!
    
    reg [18:0] mcu_addr;
    reg [7:0]  shift_reg;
    reg [7:0]  data_latch;
    reg [1:0]  byte_cnt;
    reg [2:0]  bit_cnt;
    reg        receiving_data;
    reg        mcu_write_req;

    always @(posedge CLK_50MHZ) begin
        if (spi_cs) begin
            // Сброс счетчиков при неактивном Chip Select
            byte_cnt       <= 0;
            bit_cnt        <= 0;
            receiving_data <= 0;
            mcu_write_req  <= 0;
        end else if (spi_sck_rise) begin
            bit_cnt <= bit_cnt + 1;

            if (!receiving_data) begin
                // Принимаем первые 3 байта (24 бита) как стартовый адрес
                mcu_addr <= {mcu_addr[17:0], spi_mosi};
                if (bit_cnt == 7) begin
                    if (byte_cnt == 2) begin
                        receiving_data <= 1; // Конец адреса, дальше идут данные
                    end else begin
                        byte_cnt <= byte_cnt + 1;
                    end
                end
            end else begin
                // Собираем полезные данные
                shift_reg <= {shift_reg[6:0], spi_mosi};
                if (bit_cnt == 7) begin
                    data_latch    <= {shift_reg[6:0], spi_mosi}; // Фиксируем байт
                    mcu_write_req <= 1; // Подаем сигнал на запись в SRAM
                end else begin
                    mcu_write_req <= 0;
                end
            end
        end else begin
             mcu_write_req <= 0;
        end
    end

    // Аппаратный генератор импульса записи в SRAM
    reg [1:0] sram_we_state;
    reg       mcu_sram_we;

    always @(posedge CLK_50MHZ) begin
        if (mcu_mode) begin
            if (mcu_write_req && sram_we_state == 0) begin
                mcu_sram_we   <= 1; 
                sram_we_state <= 1;
            end else if (sram_we_state == 1) begin
                sram_we_state <= 2; // Удержание импульса (20 нс)
            end else if (sram_we_state == 2) begin
                mcu_sram_we   <= 0;
                mcu_addr      <= mcu_addr + 1; // Автоинкремент адреса
                sram_we_state <= 0;
            end
        end else begin
            sram_we_state <= 0;
            mcu_sram_we   <= 0;
        end
    end

    // =========================================================================
    // 3. ЛОГИКА ШИНЫ ZX SPECTRUM И МУЛЬТИПЛЕКСИРОВАНИЕ
    // =========================================================================
    
    // Обнаружение обращения к области ПЗУ (0x0000 - 0x3FFF)
    // Z80 опускает MREQ, A15 и A14 равны 0. RFSH должен быть неактивен (1).
    wire zx_rom_access = (~Z80_MREQ_N & ~Z80_A[15] & ~Z80_A[14] & Z80_RFSH_N);

    // Блокируем внутреннее ПЗУ Спектрума во время работы нашего картриджа
    assign Z80_ROMCS_N = mcu_mode ? 1'b1 : ~zx_rom_access;

    // Управление направлением буфера данных 74LVC245
    // 0 = Чтение Спектрумом (SRAM -> Z80), 1 = Запись Спектрумом (Z80 -> SRAM)
    assign BUF_DIR = ~Z80_RD_N ? 1'b0 : 1'b1;

    // Буфер включен только когда Z80 активен и обращается к нашей памяти
    assign BUF_OE_N = mcu_mode ? 1'b1 : ~zx_rom_access;

    // Коммутация адреса памяти
    assign SRAM_A = mcu_mode ? mcu_addr : {3'b000, Z80_A};

    // Коммутация управления памятью
    assign SRAM_CE_N = mcu_mode ? spi_cs : Z80_MREQ_N;
    assign SRAM_OE_N = mcu_mode ? 1'b1   : Z80_RD_N;
    assign SRAM_WE_N = mcu_mode ? ~mcu_sram_we : Z80_WR_N;

    // Коммутация шины данных
    // Во время загрузки ПЛИС выдает байт, в режиме игры — ножки переходят в высокоимпедансное состояние (Z)
    assign D_33V = (mcu_mode && sram_we_state != 0) ? data_latch : 8'hZZ;

endmodule
module alf_cartridge_top (
    input  wire        clk_50m,      // PIN_12 (/CLK_50MHZ)
    input  wire        mcu_active,   // PIN_35 (/MCU_ACTIVE)

    input  wire        spi_cs_n,     // PIN_48 (/SPI_CS)
    input  wire        spi_sck,      // PIN_49 (/SPI_SCK)
    input  wire        spi_mosi,     // PIN_50 (/SPI_MOSI)
    output wire        spi_miso,     // PIN_47 (/SPI_MISO)

    input  wire [13:0] z80_a,        // PIN_21..14, 8..3 (A0..A13 от U2, U3)
    input  wire        pbank,        // PIN_28 (/PBANK_33V)
    input  wire        romc_n,       // PIN_27 (/ROMC_33V)
    input  wire        z80_rst_n,    // PIN_34 (/Z80_RST_33V)
    input  wire        z80_mreq_n,   // PIN_97 (/Z80_MREQ)
    input  wire        z80_wr_n,     // PIN_98 (/Z80_WR)
    input  wire        z80_rd_n,     // PIN_99 (/Z80_RD)
    input  wire        z80_rfsh_n,   // PIN_100(/Z80_RFSH)
    input  wire        z80_iorq_n,   // PIN_1  (/Z80_IORQ)
    input  wire        z80_m1_n,     // PIN_2  (/Z80_M1)

    inout  wire [7:0]  d_33v,        // PIN_38,37,36,39,40,41,42,43

    output wire        buf_dir,      // PIN_29 (/BUF_DIR)
    output wire        buf_oe_n,     // PIN_26 (/BUF_OE)

    output wire [18:0] sram_a,
    output wire        sram_ce_n,    // PIN_44 (/SRAM_CE)
    output wire        sram_oe_n,    // PIN_55 (/SRAM_OE)
    output wire        sram_we_n     // PIN_69 (/SRAM_WE)
);


// =========================================================================
    // 1. ЭМУЛЯЦИЯ МАППЕРА «ЭЛЬФА» С ЦИФРОВОЙ RC-ЗАДЕРЖКОЙ ФРОНТА PBANK (~112 нс)
    // =========================================================================
    reg [5:0] bank_reg;
    reg [2:0] pbank_sync;
    reg [3:0] pbank_delay_cnt;
    reg       pbank_latched;

    always @(posedge clk_50m or negedge z80_rst_n) begin
        if (!z80_rst_n) begin
            bank_reg        <= 6'b000000;
            pbank_sync      <= 3'b000;
            pbank_delay_cnt <= 4'd0;
            pbank_latched   <= 1'b0;
        end else begin
            pbank_sync <= {pbank_sync[1:0], pbank};

            if (!pbank_sync[2]) begin
                // Линия PBANK неактивна — сбрасываем счетчик задержки
                pbank_delay_cnt <= 4'd0;
                pbank_latched   <= 1'b0;
            end else if (!pbank_latched) begin
                // Ждем 6 тактов после синхронизатора (~112 нс от переднего фронта PBANK),
                // чтобы данные на шине D0..D5 гарантированно устоялись в середине цикла OUT ($5F),A
                if (pbank_delay_cnt == 4'd6) begin
                    bank_reg      <= d_33v[5:0];
                    pbank_latched <= 1'b1;
                end else begin
                    pbank_delay_cnt <= pbank_delay_cnt + 4'd1;
                end
            end
        end
    end
	 
    wire cart_read_en = (~romc_n) & (~bank_reg[5]);
	 // Чтение из SRAM и разворот буфера U5 наружу разрешены ТОЛЬКО при активном /RD = 0!
    // Во время записи в порт $5F (/RD = 1) буфер U5 открыт на вход (A -> B) для приема данных.
	 
    wire [18:0] game_addr = {bank_reg[4], bank_reg[3:0], z80_a[13:0]};

    // 2. SPI-мост для BIST и загрузки образов от Raspberry Pi Pico
    reg [2:0] spi_sck_s;
    reg [1:0] spi_cs_s;
    reg       spi_mosi_s;

    always @(posedge clk_50m) begin
        spi_sck_s  <= {spi_sck_s[1:0], spi_sck};
        spi_cs_s   <= {spi_cs_s[0], spi_cs_n};
        spi_mosi_s <= spi_mosi;
    end

    wire sck_rise  = (spi_sck_s[2:1] == 2'b01);
    wire sck_fall  = (spi_sck_s[2:1] == 2'b10);
    wire cs_active = ~spi_cs_s[1];

    reg [2:0]  bit_cnt;
    reg [2:0]  state;
    reg [7:0]  rx_shift;
    reg [7:0]  tx_shift;
    reg [7:0]  cmd_reg;
    reg [18:0] mcu_addr;
    reg [7:0]  mcu_data_out;
    reg        we_pulse;
    reg [2:0]  we_cnt;

    wire [7:0] rx_next = {rx_shift[6:0], spi_mosi_s};

    always @(posedge clk_50m) begin
        if (!cs_active) begin
            bit_cnt  <= 3'd0;
            state    <= 3'd0;
            we_pulse <= 1'b0;
            we_cnt   <= 3'd0;
        end else begin
            if (we_pulse) begin
                if (we_cnt == 3'd3) begin
                    we_pulse <= 1'b0;
                    mcu_addr <= mcu_addr + 19'd1;
                end else begin
                    we_cnt <= we_cnt + 3'd1;
                end
            end

            if (sck_rise) begin
                rx_shift <= rx_next;
                bit_cnt  <= bit_cnt + 3'd1;
                if (bit_cnt == 3'd7) begin
                    case (state)
                        3'd0: begin cmd_reg         <= rx_next;      state <= 3'd1; end
                        3'd1: begin mcu_addr[18:16] <= rx_next[2:0]; state <= 3'd2; end
                        3'd2: begin mcu_addr[15:8]  <= rx_next;      state <= 3'd3; end
                        3'd3: begin mcu_addr[7:0]   <= rx_next;      state <= 3'd4; end
                        3'd4: begin
                            if (cmd_reg == 8'h01) begin
                                mcu_data_out <= rx_next;
                                we_pulse     <= 1'b1;
                                we_cnt       <= 3'd0;
                            end else if (cmd_reg == 8'h02) begin
                                mcu_addr     <= mcu_addr + 19'd1;
                            end
                        end
                    endcase
                end
            end

            if (sck_fall) begin
                if (bit_cnt == 3'd0) tx_shift <= d_33v;
                else                 tx_shift <= {tx_shift[6:0], 1'b0};
            end
        end
    end

    assign spi_miso = cs_active ? tx_shift[7] : 1'bz;

    // 3. Арбитраж шины
    wire mcu_wr_en = mcu_active && (cmd_reg == 8'h01) && (state == 3'd4);
    wire mcu_rd_en = mcu_active && (cmd_reg == 8'h02) && (state == 3'd4);

    assign d_33v     = mcu_wr_en  ? mcu_data_out : 8'bz;
    assign sram_a    = mcu_active ? mcu_addr     : game_addr;
    assign sram_ce_n = mcu_active ? 1'b0         : ~cart_read_en;
    assign sram_oe_n = mcu_active ? ~mcu_rd_en   : ~cart_read_en;
    assign sram_we_n = mcu_active ? ~we_pulse    : 1'b1;

    assign buf_oe_n  = mcu_active   ? 1'b1 : 1'b0;
    assign buf_dir   = cart_read_en ? 1'b0 : 1'b1;

endmodule
# Спецификация компонентов (BOM) — SCARRY v1 (`alf-scar`)

| Поз. обозначение | Наименование / Номинал | Корпус (Footprint в KiCad) | Кол-во | Назначение |
| :--- | :--- | :--- | :---: | :--- |
| **`A1`** | `RaspberryPi_Pico` (`RP2040`) | `Module:RaspberryPi_Pico_SMD` | 1 | Управляющий МК, генератор 50 МГц, чтение microSD |
| **`U1`** | `AMS1117-3.3` | `Package_TO_SOT_SMD:SOT-223-3_TabPin2` | 1 | Линейный стабилизатор питания `+5V -> +3.3V` (1 А) |
| **`U2, U3, U4, U5`**| `74LVT245` (или `74LVC245A`) | `Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm` | 4 | Шинные формирователи 5V-tolerant для связи с Z80 |
| **`U6`** | `AS6C4008-55PCN` (или `-55SIN`) | `Package_SO:TSOP-II-32` / `DIP-32` | 1 | Статическое ОЗУ 512 КБ (512K x 8 бит) |
| **`U7`** | `SSD1306-0.91-I2C` (128x32) | `Display:ER_OLEDM0.91_1x-I2C` | 1 | OLED-дисплей 0.91 дюйма (I2C, 4 вывода) |
| **`U8`** | `EPM240T100C5N` | `Package_QFP:TQFP-100_14x14mm_P0.5mm` | 1 | ПЛИС Altera MAX II (маппер Эльфа и SPI-мост) |
| **`J1`** | `Conn_02x05_Odd_Even` (JTAG) | `Connector_IDC:IDC-Header_2x05_P2.54mm` | 1 | Разъем программирования ПЛИС Altera USB-Blaster |
| **`J2`** | `Micro_SD_Card` | `Lib:J_SD_Card-micro_socket_A` | 1 | Слот карты памяти microSD |
| **`XP1`** | `ALF-LAMEL` (48 pin) | `library:ALF_LAMEL` | 1 | Краевой разъем картриджа приставки «Эльф» |
| **`Q1`** | `BCR108` (или `BC847` + 10K) | `Package_TO_SOT_SMD:SOT-23` | 1 | Цифровой NPN-транзистор удержания сброса `/*RST/` |
| **`SW1, SW2, SW3`** | `SW_PUSH_6mm` (`UP, OK, DOWN`) | `Button_Switch_THT:SW_PUSH_6mm` | 3 | Кнопки управления меню картриджа |
| **`C4, C11`** | `10uF` (тантал, 10V/16V) | `Capacitor_Tantalum_SMD:CP_EIA-3216-18` | 2 | Фильтрация входного (`+5V`) и выходного (`+3.3V`) питания |
| **`C1..C3, C5..C10, C12..C22`** | `100nF` (керамика X7R) | `Capacitor_SMD:C_0805_2012Metric` | 20 | Блокировочные конденсаторы ИС и ФНЧ кнопок |
| **`R1, R3, R5..R9, R11`** | `10K` | `Resistor_SMD:R_0805_2012Metric` | 8 | Подтяжки кнопок (`SW1..SW3`) и линий microSD (`J2`) |
| **`R2, R4, R10`** | `1K` | `Resistor_SMD:R_0805_2012Metric` | 3 | Токоограничивающие резисторы RC-цепочек кнопок |
| **`R12, R13`** | `4.7K` | `Resistor_SMD:R_0805_2012Metric` | 2 | Подтяжка шины I2C (`SDA`, `SCL`) экрана `U7` |

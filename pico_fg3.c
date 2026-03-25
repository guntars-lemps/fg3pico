#include <stdio.h>
#include <pico/stdlib.h>
#include <string.h>
#include <hardware/flash.h>
#include <hardware/pio.h>
#include <hardware/sync.h>
#include <hardware/uart.h>
#include <hardware/clocks.h>
#include <tusb.h>
#include <hardware/structs/usb.h>
#include "pio_fg3.pio.h"
#include "pico_fg3.h"

#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define F1_PIN 2
#define F2_PIN 3
#define F3_PIN 4

#define UART_ID uart0
#define BAUD_RATE 19200

/*----------------------------------

<<<<<<<<<< UART commands >>>>>>>>>>

  For CRC checksum calculation is used CRC16/MODBUS algorithm, each TX/RX packet contains additional two CRC16 bytes at the end

  Data has big-endian byte ordering, i.e., byte order starts with highest byte and ends with lowest byte

*** Command 01 - Set frequencies, if ON value is 0 then frequency is muted (off)

  Format: (39 bytes)

  byte 0      : Comamnd 01
  bytes 1-4   : Startup delay for F1, uint32
  bytes 5-8   : ON period for F1, uint32
  bytes 9-12  : OFF period for F1, uint32
  bytes 13-16 : Startup delay for F2, uint32
  bytes 17-20 : ON period for F2, uint32
  bytes 21-24 : OFF period for F2, uint32
  bytes 25-28 : Startup delay for F3, uint32
  bytes 29-32 : ON period for F3, uint32
  bytes 33-36 : OFF period for F3, uint32
  bytes 37-38 : CRC16

*** Command 02 - Stores current frequencies to the pico flash memory

  Format: (3 bytes)

  byte 0    : Comamnd 02
  bytes 1-2 : CRC16

*** Command 03 - Loads frequencies from the pico flash memory, this is done also at the board startup or reset

  Format: (3 bytes)

  byte 0    : Comamnd 03
  bytes 1-2 : CRC16

Possible responses for commands 01-03 :
  00 + CRC16 = OK, Command executes
  01 + CRC16 = BAD command (frequencies out of range or CRC does not match)
  02 + CRC16 = BAD data in the flash memory (CRC does not match)

*** Command 04 - Checking generator capabilities, can be used also to check if board is connected

  Command format: (3 bytes)

  byte 0    : Comamnd 04
  bytes 1-2 : CRC16

  Response format: (19 bytes)

  byte 0      : Command 04
  bytes 1-4   : Minimal frequency value, uint32
  bytes 5-8   : Maximal frequency value, uint32
  bytes 9-12  : CPU ticks per 1 frequency unit, unit32
  bytes 13-16 : CPU ticks per 1 second, uint32
  bytes 17-18 : CRC16

*/

// Calculate checksum
uint16_t calculate_crc(uint8_t* buf, uint8_t buflen)
{
    uint16_t crc = 0xffff;
    uint8_t p = 0;

    for (p = 0; p < buflen; p++) {
        crc ^= (uint16_t)buf[p];
        uint8_t i;
        for (i = 0; i < 8; i++) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xa001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool check_crc(uint8_t* buffer, uint8_t buflen)
{
    if (buflen < 3) {
        return false;
    }
    uint16_t buffer_crc = ((uint16_t)buffer[buflen - 2] << 8) | (uint16_t)buffer[buflen - 1];
    uint16_t calculated_crc = calculate_crc(buffer, buflen - 2);
    return buffer_crc == calculated_crc;
}

// Add Checksum to the buffer
void put_checksum(uint8_t* buffer, uint8_t* buflen)
{
    uint16_t crc = calculate_crc(buffer, *buflen);
    uint8_t high_byte = (uint8_t)((crc & 0xff00) >> 8);
    uint8_t low_byte = (uint8_t)(crc & 0x00ff);
    buffer[(*buflen)++] = high_byte;
    buffer[(*buflen)++] = low_byte;
}

// Put uint32_t to the buffer
void put_uint32(uint8_t* buffer, uint8_t* buflen, uint32_t value)
{
    uint8_t highest_byte = (uint8_t)((value & 0xff000000) >> 24);
    uint8_t high_byte = (uint8_t)((value & 0x00ff0000) >> 16);
    uint8_t low_byte = (uint8_t)((value & 0x0000ff00) >> 8);
    uint8_t lowest_byte = (uint8_t)(value & 0x000000ff);
    buffer[(*buflen)++] = highest_byte;
    buffer[(*buflen)++] = high_byte;
    buffer[(*buflen)++] = low_byte;
    buffer[(*buflen)++] = lowest_byte;
}

// Get uint32_t from the buffer
uint32_t get_uint32(uint8_t* buffer)
{
    uint32_t highest_byte = buffer[0];
    uint32_t high_byte = buffer[1];
    uint32_t low_byte = buffer[2];
    uint32_t lowest_byte = buffer[3];
    return (highest_byte << 24) | (high_byte << 16) | (low_byte << 8) | lowest_byte;
}

bool is_usb_connected()
{
    return usb_hw->sie_status & (1 << 16);
}

bool uart_data_ready()
{
    if (use_usb) {
        return tud_cdc_n_available(0) > 0;
    } else {
        return uart_is_readable(UART_ID);
    }
}

uint8_t uart_read()
{
    uint8_t buf;

    if (use_usb) {
        tud_cdc_n_read(0, &buf, 1);
        return buf;
    } else {
        return uart_getc(UART_ID);
    }
}

void uart_write(uint8_t* buffer, uint8_t buflen)
{
    if (use_usb) {
        tud_cdc_write(buffer, buflen);
        tud_cdc_n_write_flush(0);
    } else {
        uart_write_blocking(UART_ID, buffer, buflen);
    }
}

// try to load buffer from flash memory
bool load_from_flash(uint8_t* buffer)
{
    uint8_t buflen = 39;
    uint8_t* flash_addr = (uint8_t*)(XIP_BASE + (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE));
    memcpy(buffer, flash_addr, buflen);
    return check_crc(buffer, buflen);
}


void store_to_flash(uint8_t* buffer, uint8_t buflen)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase((PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE), FLASH_SECTOR_SIZE);
    flash_range_program((PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE), buffer, buflen);
    restore_interrupts (ints);
}

// initialize pio
void init_pio(PIO pio, uint sm, uint offset, uint pin)
{
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_sm_config c = fg3pio_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);
    pio_sm_init(pio, sm, offset, &c);
}


void setup_pios(uint* pio_program_offset)
{
    *pio_program_offset = pio_add_program(pio0, &fg3pio_program);

    init_pio(pio0, 0, *pio_program_offset, F1_PIN);
    init_pio(pio0, 1, *pio_program_offset, F2_PIN);
    init_pio(pio0, 2, *pio_program_offset, F3_PIN);
}

// Load frequencies to the pio0
bool set_frequencies(uint8_t* buffer, uint pio_program_offset)
{
    // Disable all 3 state machines
    pio_set_sm_mask_enabled(pio0, 0x07, false);

    // set all pins to low
    gpio_put_masked((1 << F1_PIN) | (1 << F2_PIN) | (1 << F3_PIN), 0);

    // Load frequencies to the state machines FIFO buffers

    uint32_t f1_delay = get_uint32(buffer + 1);
    uint32_t f1_on = get_uint32(buffer + 5);
    uint32_t f1_off = get_uint32(buffer + 9);

    uint32_t f2_delay = get_uint32(buffer + 13);
    uint32_t f2_on = get_uint32(buffer + 17);
    uint32_t f2_off = get_uint32(buffer + 21);

    uint32_t f3_delay = get_uint32(buffer + 25);
    uint32_t f3_on = get_uint32(buffer + 29);
    uint32_t f3_off = get_uint32(buffer + 33);

    if ((f1_delay < 3) || ((f1_on != 0) && (f1_on < 3)) || (f1_off < 3) ||
        (f2_delay < 3) || ((f2_on != 0) && (f2_on < 3)) || (f2_off < 3) ||
        (f3_delay < 3) || ((f3_on != 0) && (f3_on < 3)) || (f3_off < 3)) {
        return false;
    }

    // write F1 frequencies to sm0 fifo buffer
    pio0->rxf_putget[0][0] = f1_delay - 3;
    pio0->rxf_putget[0][1] = f1_on - 3;
    pio0->rxf_putget[0][2] = f1_off - 3;

    // write F2 frequencies to sm1 fifo buffer
    pio0->rxf_putget[1][0] = f2_delay - 3;
    pio0->rxf_putget[1][1] = f2_on - 3;
    pio0->rxf_putget[1][2] = f2_off - 3;

    // write F3 frequencies to sm2 fifo buffer
    pio0->rxf_putget[2][0] = f3_delay - 3;
    pio0->rxf_putget[2][1] = f3_on - 3;
    pio0->rxf_putget[2][2] = f3_off - 3;

    // reset all state machine PC pointers
    pio_sm_exec(pio0, 0, pio_encode_jmp(pio_program_offset));
    pio_sm_exec(pio1, 0, pio_encode_jmp(pio_program_offset));
    pio_sm_exec(pio2, 0, pio_encode_jmp(pio_program_offset));

    // Enable state machines which have "on" > 0
    pio_set_sm_mask_enabled(pio0, ((f1_on > 0) ? 0x01 : 0) | ((f2_on > 0) ? 0x02 : 0) | ((f3_on > 0) ? 0x04 : 0), true);
    return true;
}

void send_ok()
{
    uint8_t output_buffer[256];
    uint8_t output_buflen = 0;
    output_buffer[output_buflen++] = 0x00;
    put_checksum(output_buffer, &output_buflen);
    uart_write(output_buffer, output_buflen);
}

void send_bad()
{
    uint8_t output_buffer[256];
    uint8_t output_buflen = 0;
    output_buffer[output_buflen++] = 0x01;
    put_checksum(output_buffer, &output_buflen);
    uart_write(output_buffer, output_buflen);
}

void send_bad_data()
{
    uint8_t output_buffer[256];
    uint8_t output_buflen = 0;
    output_buffer[output_buflen++] = 0x02;
    put_checksum(output_buffer, &output_buflen);
    uart_write(output_buffer, output_buflen);
}

void send_capabilities()
{
    uint8_t output_buffer[256];
    uint8_t output_buflen = 0;

    const uint32_t min_freq = 3;
    const uint32_t max_freq = 0xffffffff;
    const uint32_t ticks_per_unit = 1;
    const uint32_t ticks_per_second = 200000000;

    output_buffer[output_buflen++] = 0x04;
    put_uint32(output_buffer, &output_buflen, min_freq);
    put_uint32(output_buffer, &output_buflen, max_freq);
    put_uint32(output_buffer, &output_buflen, ticks_per_unit);
    put_uint32(output_buffer, &output_buflen, ticks_per_second);
    put_checksum(output_buffer, &output_buflen);

    uart_write(output_buffer, output_buflen);
}

int main()
{
    uint8_t input_buffer[256];
    uint8_t work_buffer[256];

    uint8_t input_bufferlen;

    uint pio_program_offset;

    gpio_init_mask((1 << F1_PIN) | (1 << F2_PIN) | (1 << F3_PIN));
    gpio_set_dir_out_masked((1 << F1_PIN) | (1 << F2_PIN) | (1 << F3_PIN));
    gpio_put_masked((1 << F1_PIN) | (1 << F2_PIN) | (1 << F3_PIN), 0);

    // Set CPU frequency to 200Mhz
    set_sys_clock_khz(200000, true);

    // init USB serial device
    stdio_init_all();

    setup_pios(&pio_program_offset);

    if (!load_from_flash(work_buffer)) {
        // mute all if there are no frequencies stored
        memset(work_buffer, 0, sizeof(work_buffer));
    }
    set_frequencies(work_buffer, pio_program_offset);

    use_usb = is_usb_connected();

    // Set up UART speed.
    uart_init(UART_ID, BAUD_RATE);
    uart_set_translate_crlf(UART_ID, false);

    // Set UART TX and RX pins
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    uint8_t input_buflen = 0;

    // Do UART comamnds processing in the loop
    while (true) {

        // if usb is connected then use UART over USB
        use_usb = is_usb_connected();

        tud_task();

        // If there is byte received then read it and put in the input buffer
        if (uart_data_ready()) {
            uint8_t byte = uart_read();
            input_buffer[input_buflen++] = byte;
        }

        // Check if input buffer contains any valid command
        uint8_t command = input_buffer[0];

        switch (command) {

            case 0x01:
                if (input_buflen < 39) {
                    // receive more bytes
                    break;
                }
                if (!check_crc(input_buffer, input_buflen)) {
                    send_bad();
                    input_buflen = 0;
                    break;
                }
                if (set_frequencies(input_buffer, pio_program_offset)) {
                    memcpy(work_buffer, input_buffer, input_buflen);
                    send_ok();
                } else {
                    // bad frequencies (out of range)
                    send_bad();
                }
                input_buflen = 0;
                break;

            case 0x02:
                // code block
                if (input_buflen < 3) {
                    // receive more bytes
                    break;
                }
                if (!check_crc(input_buffer, input_buflen)) {
                    send_bad();
                    input_buflen = 0;
                    break;
                }
                store_to_flash(work_buffer, 39);
                send_ok();
                input_buflen = 0;
                break;

            case 0x03:
                // code block
                if (input_buflen < 3) {
                    // receive more bytes
                    break;
                }
                if (!check_crc(input_buffer, input_buflen)) {
                    send_bad();
                    input_buflen = 0;
                    break;
                }
                if (load_from_flash(input_buffer)) {
                    if (set_frequencies(input_buffer, pio_program_offset)) {
                        memcpy(work_buffer, input_buffer, input_buflen);
                        send_ok();
                    } else {
                        send_bad();
                    }
                } else {
                    send_bad_data();
                }
                input_buflen = 0;
                break;

            case 0x04:
                if (input_buflen < 3) {
                    // receive more bytes
                    break;
                }
                if (!check_crc(input_buffer, input_buflen)) {
                    send_bad();
                    input_buflen = 0;
                    break;
                }
                send_capabilities();
                input_buflen = 0;
                break;

            default:
                // Unknown command, reset input buffer
                input_buflen = 0;
        }
    }
}

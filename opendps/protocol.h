/* 
 * The MIT License (MIT)
 * 
 * Copyright (c) 2017 Johan Kanflo (github.com/kanflo)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/* This module defines the serial interface protocol. Everything you can do via
 * the buttons and dial on the DPS can be instrumented via the serial port.
 *
 * The basic frame payload is [<cmd>] [<optional payload>]* to which the device 
 * will respond [cmd_response | <cmd>] [success] [<response data>]*
 */

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    cmd_ping = 1,
    cmd_set_vout,
    cmd_set_ilimit,
    cmd_status,
    cmd_power_enable,
    cmd_wifi_status,
    cmd_lock,
    cmd_ocp_event,
    cmd_upgrade_start,
    cmd_upgrade_data,
    cmd_response = 0x80
} command_t;

typedef enum {
    wifi_off = 0,
    wifi_connecting,
    wifi_connected,
    wifi_error,
    wifi_upgrading // Used by the ESP8266 when doing FOTA
} wifi_status_t;

typedef enum {
    upgrade_continue = 0, /** device sent go-ahead for continued upgrade */
    upgrade_bootcom_error, /** device found errors in the bootcom data */
    upgrade_crc_error, /** crc verification of downloaded upgrade failed */
    upgrade_erase_error, /** device encountered error while erasing flash */
    upgrade_flash_error, /** device encountered error while writing to flash */
    upgrade_overflow_error, /** downloaded image would overflow flash */
    upgrade_protocol_error, /** device received upgrade data but no upgrade start */
    upgrade_success = 16 /** device received entire firmware and crc, branch verification was successful */
} upgrade_status_t;

/** The boot will report why it entered upgrade mode */
typedef enum {
    reason_unknown = 0, /** No idea why I'm here */
    reason_forced, /** User forced via button */
    reason_past_failure, /** Past init failed */
    reason_bootcom, /** App told us via bootcom */
    reason_unfinished_upgrade, /** A previous unfinished sympathy, eh upgrade */
    reason_app_start_failed /** App returned */
} upgrade_reason_t;

#define MAX_FRAME_LENGTH (2*16) // Based on the cmd_status reponse frame (fully escaped)

/*
 * Helpers for creating frames.
 *
 * On return, 'frame' will hold a complete frame ready for transmission and the 
 * return value will be the length of the frame. If the specified 'length' of 
 * the frame buffer is not sufficient, the return value will be zero and the 
 * 'frame' buffer is left untouched.
 */
uint32_t protocol_create_response(uint8_t *frame, uint32_t length, command_t cmd, uint8_t success);
uint32_t protocol_create_ping(uint8_t *frame, uint32_t length);
uint32_t protocol_create_power_enable(uint8_t *frame, uint32_t length, uint8_t enable);
uint32_t protocol_create_vout(uint8_t *frame, uint32_t length, uint16_t vout_mv);
uint32_t protocol_create_ilimit(uint8_t *frame, uint32_t length, uint16_t ilimit_ma);
uint32_t protocol_create_status(uint8_t *frame, uint32_t length);
uint32_t protocol_create_status_response(uint8_t *frame, uint32_t length, uint16_t v_in, uint16_t v_out_setting, uint16_t v_out, uint16_t i_out, uint16_t i_limit, uint8_t power_enabled);
uint32_t protocol_create_wifi_status(uint8_t *frame, uint32_t length, wifi_status_t status);
uint32_t protocol_create_lock(uint8_t *frame, uint32_t length, uint8_t locked);
uint32_t protocol_create_ocp(uint8_t *frame, uint32_t length, uint16_t i_cut);

/*
 * Helpers for unpacking frames.
 *
 * These functions will unpack the content of the unframed payload and return
 * true. If the command byte of the frame does not match the expectation or the
 * frame is too short to unpack the expected payload, false will be returned.
 */
bool protocol_unpack_response(uint8_t *payload, uint32_t length, command_t *cmd, uint8_t *success);
bool protocol_unpack_power_enable(uint8_t *payload, uint32_t length, uint8_t *enable);
bool protocol_unpack_vout(uint8_t *payload, uint32_t length, uint16_t *vout_mv);
bool protocol_unpack_ilimit(uint8_t *payload, uint32_t length, uint16_t *ilimit_ma);
bool protocol_unpack_status_response(uint8_t *payload, uint32_t length, uint16_t *v_in, uint16_t *v_out_setting, uint16_t *v_out, uint16_t *i_out, uint16_t *i_limit, uint8_t *power_enabled);
bool protocol_unpack_wifi_status(uint8_t *payload, uint32_t length, wifi_status_t *status);
bool protocol_unpack_lock(uint8_t *payload, uint32_t length, uint8_t *locked);
bool protocol_unpack_ocp(uint8_t *payload, uint32_t length, uint16_t *i_cut);
bool protocol_unpack_upgrade_start(uint8_t *payload, uint32_t length, uint16_t *chunk_size, uint16_t *crc);


/*
 *    *** Command types ***
 *
 * === Pinging DPS ===
 * The ping command is sent by the host to check if the DPS is online.
 *
 *  HOST:   [cmd_ping]
 *  DPS:    [cmd_response | cmd_ping] [1]
 *
 *
 * === Setting desired out voltage ===
 * The voltage is specified in millivolts. The success response field will be
 * 0 if the requested voltage was outside of what the DPS can provide.
 *
 *  HOST:   [cmd_set_vout] [vout_mv(15:8)] [vout_mv(7:0)]
 *  DPS:    [cmd_response | cmd_set_vout] [<success>]
 *
 *
 * === Setting maximum current limit ===
 * The current is specified in milliampere. The success response field will be
 * 0 if the requested current was outside of what the DPS can provide.
 *
 *  HOST:   [cmd_set_ilimit] [ilimit_ma(15:8)] [ilimit_ma(7:0)]
 *  DPS:    [cmd_response | cmd_set_ilimit] [<success>]
 *
 *
 * === Reading the status of the DPS ===
 * This command retrieves V_in, V_out, I_out, I_limit, power enable. Voltage
 * and currents are all in the 'milli' range.
 *
 *  HOST:   [cmd_status]
 *  DPS:    [cmd_response | cmd_status] [1] [V_in(15:8)] [V_in(7:0)] [V_out_setting(15:8)] [V_out_setting(7:0)] [V_out(15:8)] [V_out(7:0)] [I_out(15:8)] [I_out(7:0)] [I_limit(15:8)] [I_limit(7:0)] [<power enable>]
 *
 *
 * === Enabling/disabling power output ===
 * This command is used to enable or disable power output. Enable = 1 will
 * obviously enable :)
 *
 *  HOST:   [cmd_power_enable] [<enable>]
 *  DPS:    [cmd_response | cmd_power_enable] [1]
 *
 *
 * === Setting wifi status ===
 * This command is used to set the wifi indicator on the screen. Status will be
 * one of the wifi_status_t enums
 *
 *  HOST:   [cmd_wifi_status] [<wifi_status_t>]
 *  DPS:    [cmd_response | cmd_wifi_status] [1]
 *
 *
 * === Locking the controls ===
 * This command is used to lock or unlock the controls.
 * lock = 1 will do just that
 *
 *  HOST:   [cmd_lock] [<lock>]
 *  DPS:    [cmd_response | cmd_lock] [1]
 *
 *
 * === Overcurrent protection event controls ===
 * If the DPS detects overcurrent, it will send this frame with the current
 * that caused the protection to kick in (in milliamperes).
 * The DPS does not expect a response
 *
 *  DPS:    [cmd_ocp_event] [I_cut(7:0)] [I_cut(15:8)]
 *  HOST:   none
 *
 * === DPS upgrade sessions ===
 * When the cmd_upgrade_start packet is received, the device prepares for
 * an upgrade session:
 *  1. The upgrade packet chunk size is determined based on the host's request
 *     and is written into the bootcom RAM in addition with the 16 bit crc of
 *     the new firmware. and the upgrade magick.
 *  2. The device restarts.
 *  3. The booloader detecs the upgrade magic in the bootcom RAM.
 *  4. The booloader sets the upgrade flag in the PAST.
 *  5. The bootloader initializes the UART, sends the cmd_upgrade_start ack and
 *     prepares for download.
 *  6. The bootloader receives the upgrade packets, writes the data to flash
 *     and acks each packet.
 *  7. When the last packet has been received, the bootloader clears the upgrade
 *     flag in the PAST and boots the app.
 *  8. The host pings the app to check the new firmware started.
 *
 *  HOST:     [cmd_upgrade_start] [chunk_size:16] [crc:16]
 *  DPS (BL): [cmd_response | cmd_upgrade_start] [<upgrade_status_t>] [<chunk_size:16>]  [<upgrade_reason_t:8>]
 *
 * The host will send packets of the agreed chunk size with the device 
 * acknowledging each packet once crc checked and written to flash. A packet
 * smaller than the chunk size or with zero payload indicates the end of the
 * upgrade session. The device will now return the outcome of the 32 bit crc
 * check of the new firmware and continue on step 7 above.
 *
 * The upgrade data packets have the following format with the payload size
 * expected to be equal to what was aggreed upon in the cmd_upgrade_start packet.
 *
 *  HOST:   [cmd_upgrade_data] [<payload>]+
 *  DPS BL: [cmd_response | cmd_upgrade_data] [<upgrade_status_t>]
 *
 */

#endif // __PROTOCOL_H__
#pragma once

#include <cstddef>
#include <cstdint>

struct embedded_audio;

namespace js8core::android {

bool usb_serial_bridge_ready();
bool usb_serial_open(int device_id,
                     int port_index,
                     int baud_rate,
                     int data_bits,
                     int stop_bits,
                     int parity);
int usb_serial_read(std::uint8_t* buffer, std::size_t length, int timeout_ms);
int usb_serial_write(const std::uint8_t* buffer, std::size_t length, int timeout_ms);
int usb_serial_set_rts(bool enabled);
int usb_serial_set_dtr(bool enabled);
int usb_serial_flush();
void usb_serial_close();
void usb_trusdx_select(embedded_audio* audio);
bool usb_trusdx_start();
uint64_t usb_trusdx_samples();

}  // namespace js8core::android

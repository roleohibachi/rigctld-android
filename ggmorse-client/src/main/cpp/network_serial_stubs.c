// SPDX-License-Identifier: GPL-3.0-or-later
// The pinned Android Hamlib links serial callbacks even for NET rigctl.
// This client has no Android USB or Bluetooth transport.
#define NO_SERIAL(transport, address_type) \
    int js8_android_##transport##_serial_is_ready(void) { return 0; } \
    int js8_android_##transport##_serial_open(address_type address, int port, int baud, \
                                              int data, int stop, int parity) { return -1; } \
    int js8_android_##transport##_serial_read(unsigned char *data, unsigned long size, int timeout) { return -1; } \
    int js8_android_##transport##_serial_write(const unsigned char *data, unsigned long size, int timeout) { return -1; } \
    int js8_android_##transport##_serial_set_rts(int state) { return -1; } \
    int js8_android_##transport##_serial_set_dtr(int state) { return -1; } \
    int js8_android_##transport##_serial_flush(void) { return -1; } \
    int js8_android_##transport##_serial_close(void) { return -1; }

NO_SERIAL(usb, int)
NO_SERIAL(bt, const char *)

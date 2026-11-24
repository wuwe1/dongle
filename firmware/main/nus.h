// BLE Nordic UART Service: the phone subscribes to TX for lines and writes commands to RX.
// Any generic BLE terminal (nRF Connect, LightBlue, Bluefy's Web Bluetooth) speaks it.
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef void (*nus_command_fn)(const char *line);

void nus_start(const char *name, nus_command_fn on_command);
// True while a phone is connected and subscribed to TX.
bool nus_ready(void);
// Sends one line as notifications, split to the connection's MTU. Returns false if the stack had no
// buffer for it (the caller counts it as dropped).
bool nus_send(const char *s, size_t n);

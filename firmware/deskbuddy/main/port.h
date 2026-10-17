// The board's side of app_port.h that main.c drives (port.c).
#pragma once

// The hardware self-check behind info (app_port_info): read-only. Call once, first in app_main, before the display
// starts.
void port_check_hardware(void);

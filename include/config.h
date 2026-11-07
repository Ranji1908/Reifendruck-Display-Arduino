// Hardware-Konfiguration. Pins koennen auch per build_flags in platformio.ini gesetzt werden.
#pragma once

// ------------------------------------------------ Pins (Standard: ESP32-C3 SuperMini)
// Bewusst keine Strapping-Pins (GPIO 2, 8, 9) verwendet.
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 5
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 6
#endif
#ifndef PIN_ENC_A          // Drehgeber CLK
#define PIN_ENC_A 3
#endif
#ifndef PIN_ENC_B          // Drehgeber DT
#define PIN_ENC_B 4
#endif
#ifndef PIN_ENC_BTN        // Drehgeber SW (Taster nach GND)
#define PIN_ENC_BTN 10
#endif
#ifndef PIN_BUZZER         // aktiver Summer ueber NPN-Transistor
#define PIN_BUZZER 7
#endif

// ------------------------------------------------ Display
// 1 = 1,3" SH1106 (empfohlen, groesser), 0 = 0,96" SSD1306
#ifndef DISPLAY_SH1106
#define DISPLAY_SH1106 1
#endif
// 1 = Display um 180 Grad drehen (je nach Einbaulage)
#ifndef DISPLAY_FLIP
#define DISPLAY_FLIP 0
#endif

// ------------------------------------------------ Bedienung
#ifndef ENC_STEPS_PER_DETENT  // KY-040: meist 4, manche Drehgeber: 2
#define ENC_STEPS_PER_DETENT 4
#endif
#ifndef ENC_REVERSE           // 1 = Drehrichtung umkehren
#define ENC_REVERSE 0
#endif
// 1 = alle empfangenen BLE-Herstellerdaten seriell ausgeben (Sensor-Diagnose)
#ifndef DEBUG_BLE
#define DEBUG_BLE 0
#endif

#define LONG_PRESS_MS 800
#define MENU_TIMEOUT_MS 30000   // Menue schliesst sich automatisch
#define OVERLAY_MS 1500         // Einblendung nach Moduswechsel

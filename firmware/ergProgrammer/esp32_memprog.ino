/**
 * esp32_memprog.ino — ergProgrammer: programator multi-memorie + consola seriala pe ESP32
 *   FW 2.0 (2026-09-19): FARA interfata web. Shell ca la ergCAN pe serial USB (921600), telnet :23
 *   si raw :2323; fisierele (.bin) se citesc/scriu pe un URL (SET url.base); pinii, tipul memoriei
 *   si profilul de tinta (SET chip.driver=...) sunt setari in NVS; OTA CHECK/UPDATE; punte
 *   telnet :2324 <-> UART pentru consolele serverelor/routerelor.
 *   Drivere: SPI NOR 25xx, I2C 24Cxx, Microwire 93Cxx, MEC16xx (JTAG/ARC, portat din Glasgow -
 *   NEVALIDAT PE SILICIU: ID -> READ -> abia apoi ERASE/WRITE).
 *   Build: tools\flash.cmd COMx (arduino-cli din Arduino IDE 2, esp32:esp32:esp32, partitions.csv OTA).
 * @author Claude Code
 * @version 2.0
 * @changes
 *   v1.0 2026-09-11 — programator serial + web UI + 2x TFT (src/main.cpp, PlatformIO)
 *   v2.0 2026-09-19 — restructurat pe arduino-cli; web UI scos; shell telnet/raw; URL fisiere; cfg NVS;
 *                     pini la runtime; profile de tinta cu cablaj; consola seriala; OTA
 */
#include "config.h"
#include "cfg.h"
#include "pins.h"
#include "appstate.h"
#include "ui_display.h"
#include "console.h"
#include "cmd.h"
#include "serbridge.h"
#include "jtag.h"
#include "arc_debug.h"
#include "mec16xx.h"
#include "spi_nor.h"
#include "i2c_eeprom.h"
#include "microwire93c.h"

Jtag         jtag;
ArcDebug     arc(jtag);
Mec16xx      mec(arc);
SpiNor       spiNor;
I2cEeprom    i2cEe;
Microwire93c mw;

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(50);
    cfg_init();          // setari NVS (wifi, url, pini, memorie)
    pinsLoad();          // g_pin.* din setari
    appstateBegin();     // log + status pt TFT
    uiBegin();           // 2x TFT ILI9341 optionale
    cmdBegin();
    serbridgeBegin();    // UART2 daca ser.on=1
    conBegin();          // WiFi STA/AP + telnet/raw/punte
    Serial.printf("# %s %s gata. HELP pentru comenzi. WiFi: %s\n", FW_NAME, FW_VERSION, cfg_str("wifi.ssid"));
}

void loop() {
    conLoop();           // WiFi tick + clienti + linii de comanda (serial/telnet/raw) + punte seriala
    uiTick();            // redeseneaza TFT daca s-a schimbat ceva
}

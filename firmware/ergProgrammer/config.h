/**
 * config.h — esp32_memprog: constante compilate + VALORI IMPLICITE pentru setari
 * @author Claude Code
 * @version 2.0
 * @changes
 *   v1.0 2026-09-11 — creat initial (pini fixi la compilare, interfata web)
 *   v2.0 2026-09-19 — FW 2.0: FARA interfata web. Shell pe telnet (23) + serial + port raw (2323)
 *                     + punte serial-console (2324). Fisierele se citesc/scriu de pe un URL
 *                     (SET url.base). Pinii si tipul de memorie sunt SETARI in NVS (cfg.h),
 *                     de aceea aici rama doar valorile IMPLICITE (PIN_DEF_*). OTA CHECK/UPDATE.
 *
 * IMPORTANT electric: totul e 3.3V. Tinta (placa T490, cip SPI, consola unui router)
 * TREBUIE sa aiba masa comuna cu ESP32. Consolele RS232 (±12V, DB9/RJ45 Cisco) NU se
 * leaga direct — cer un MAX3232. Consolele TTL 3.3V se leaga direct; cele de 5V prin
 * divizor pe RX.
 */
#ifndef MEMPROG_CONFIG_H
#define MEMPROG_CONFIG_H

#include <Arduino.h>

#define FW_NAME     "ergProgrammer"
#define FW_VERSION  "2.1"

// ---------------------------------------------------------------------------
// Pini IMPLICITI (se pot schimba din shell: SET pin.tck 25 / PINS)
// ---------------------------------------------------------------------------
// JTAG bit-bang (MEC16xx / ARC): TCK/TMS/TDI iesiri, TDO intrare, TRST optional (-1).
#define PIN_DEF_JTAG_TCK   "25"
#define PIN_DEF_JTAG_TMS   "33"
#define PIN_DEF_JTAG_TDI   "18"
#define PIN_DEF_JTAG_TDO   "34"      // GPIO34 = input-only, ideal pentru TDO
#define PIN_DEF_JTAG_TRST  "-1"
// Intarziere per semi-perioada TCK (us). 2 us => ~250 kHz. Coboara la 1/0 dupa ce
// validezi pe banc (ID + dump stabil).
#define DEF_JTAG_HALF_US   "2"

// SPI NOR 25-series (hardware SPI / VSPI)
#define PIN_DEF_SPI_SCK    "18"
#define PIN_DEF_SPI_MISO   "19"
#define PIN_DEF_SPI_MOSI   "23"
#define PIN_DEF_SPI_CS     "5"
#define DEF_SPI_HZ         "8000000"

// I2C EEPROM 24-series
#define PIN_DEF_I2C_SDA    "21"
#define PIN_DEF_I2C_SCL    "22"
#define DEF_I2C_HZ         "100000"

// Microwire 93C
#define PIN_DEF_MW_CS      "17"
#define PIN_DEF_MW_SK      "16"
#define PIN_DEF_MW_DI      "4"       // ESP32 -> DI cip
#define PIN_DEF_MW_DO      "35"      // DO cip -> ESP32 (input-only OK)

// Serial console (punte telnet <-> UART pentru servere/routere). UART2.
#define PIN_DEF_SER_TX     "27"      // ESP32 TX -> RX tinta
#define PIN_DEF_SER_RX     "26"      // ESP32 RX <- TX tinta
#define PIN_DEF_SER_DTR    "-1"      // optional (doar cu MAX3232 / semnale de control)
#define PIN_DEF_SER_RTS    "-1"
#define DEF_SER_BAUD       "115200"
#define DEF_SER_FMT        "8N1"

// ---------------------------------------------------------------------------
// TFT ILI9341 x2 (refolosite de pe placa C-27J) — OPTIONALE, pini la compilare
// ---------------------------------------------------------------------------
// Rama fixe: display-ul e hardware de banc, nu se schimba de la shell.
// ATENTIE: PIN_TFT_BL(21) = PIN_DEF_I2C_SDA(21). Pe placa dedicata mut I2C pe 26/27
// SAU backlight-ul pe alt pin (vezi PINS, care semnaleaza conflictele).
#define PIN_TFT_SCK    14
#define PIN_TFT_MOSI   13
#define PIN_TFT_MISO   12
#define PIN_TFT_DC      2
#define PIN_TFT_RST    (-1)
#define PIN_TFT_CS1    15      // TFT1 = panou STARE
#define PIN_TFT_CS2    23      // TFT2 = LOG live
#define PIN_TFT_BL     21      // backlight (HIGH = aprins)

// ---------------------------------------------------------------------------
// Retea / shell
// ---------------------------------------------------------------------------
// WiFi ca la ergCAN: STA pe reteaua configurata; daca nu prinde, AP propriu de rezerva.
// Setabile din shell: SET wifi.ssid / wifi.pass / wifi.ap / net.dns (vezi cfg.cpp).
#define DEF_STA_SSID       ""   // set with: SET wifi.ssid <name>
#define DEF_STA_PASS       ""   // set with: SET wifi.pass <pass>
#define WIFI_STA_TIMEOUT_S 20            // cat incerc STA inainte de a lua o decizie
#define WIFI_AP_FALLBACK_S 120           // AP-ul de rezerva porneste abia dupa atata STA pierdut
                                         // (la PRIMA pornire pragul scurt de sus e suficient)
#define WIFI_AP_SSID       "ergProgrammer_ESP32"
#define WIFI_AP_PASS       "changeme123" // min 8 caractere (WPA2)
#define WIFI_AP_CHANNEL    6
#define WIFI_AP_MAX_CONN   2
#define MDNS_HOSTNAME      "ergprogrammer" // ergprogrammer.local

#define TELNET_PORT        23          // shell pentru om (echo, IAC, prompt)
#define RAW_PORT           2323        // shell brut, fara echo/IAC (host/memprog.py --tcp)
#define SERBRIDGE_PORT     2324        // punte directa pe UART-ul tintei (ser2net)
#define MAX_TELNET_CLIENTS 2
#define MAX_RAW_CLIENTS    2
#define MAX_BRIDGE_CLIENTS 2

// URL-ul unde se citesc/scriu fisierele (.bin). Implicit gol => READ/WRITE cer SET url.base.
#define DEF_URL_BASE   ""
#define DEF_URL_PUT    "" // absolut sau relativ la base; gol => PUT direct
#define DEF_OTA_URL    ""

// Serial USB (protocol host + log)
#define SERIAL_BAUD    921600

// Buffer maxim pe o linie de comanda (payload hex la MECPROG/SPIWR).
#define CMD_LINE_MAX   8200

#endif // MEMPROG_CONFIG_H

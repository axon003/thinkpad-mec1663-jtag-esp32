/**
 * pins.h — pinii folositi de drivere, cititi din setari (NVS) la runtime
 *   Inainte (FW 1.0) pinii erau macro-uri compilate. Acum vin din cfg (SET pin.tck 25 ...),
 *   ca sa pot muta cablajul fara sa reflashez. Driverele citesc g_pin.*.
 *   Pinii TFT rama compilati (display de banc, vezi config.h).
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0)
 */
#ifndef MEMPROG_PINS_H
#define MEMPROG_PINS_H

#include <Arduino.h>

struct PinCfg {
    int8_t  tck, tms, tdi, tdo, trst;      // JTAG
    int8_t  sck, miso, mosi, cs;           // SPI NOR
    int8_t  sda, scl;                      // I2C
    int8_t  mwcs, mwsk, mwdi, mwdo;        // Microwire
    int8_t  stx, srx, sdtr, srts;          // consola serial
    uint8_t  jtagDrv;                      // GPIO drive 0..3 pe TCK/TMS/TDI
    uint16_t jtagUs;                       // semi-perioada TCK (us)
    uint32_t spiHz, i2cHz;
};

extern PinCfg g_pin;

// Reciteste toti pinii din cfg. Se apeleaza la boot si dupa SET pin.*
void pinsLoad();

// Verifica suprapunerile intre grupurile ACTIVE si intoarce avertismentele
// (text multi-linie, gol daca nu sunt). Nu blocheaza: pe placa partajata unele
// suprapuneri sunt acceptabile daca nu folosesti driverele in acelasi timp.
String pinsConflicts(const char *memtype);

// Tabel pentru comanda PINS.
String pinsTable();

#endif // MEMPROG_PINS_H

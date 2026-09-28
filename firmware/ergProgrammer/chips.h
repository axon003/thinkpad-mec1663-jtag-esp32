/**
 * chips.h — profile de tinta ("chip.driver"): ce memorie e, ce dimensiune are si
 *   CUM SE LEAGA FIRELE. `SET chip.driver=<nume>` aplica profilul (mem.type, pini,
 *   parametri) si raspunde chiar acolo cu schema de conectare, ca sa nu caut in notite.
 *   `CHIPS` listeaza profilele.
 *
 *   Ce NU e verificat fizic e marcat explicit in text (ex: pad-urile JTAG pe placa
 *   T490 NM-B901 nu sunt documentate public) — profilul nu inventeaza puncte de test.
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0)
 */
#ifndef MEMPROG_CHIPS_H
#define MEMPROG_CHIPS_H

#include <Arduino.h>

struct ChipProfile {
    const char *name;      // cheia pentru SET chip.driver=<name>
    const char *memtype;   // mec | mecee | spi | i2c | mw | ser
    uint32_t    size;      // bytes; 0 = din ID / implicit driverului
    const char *desc;
    const char *wiring;    // schema de conectare (multi-linie)
};

size_t             chipsCount();
const ChipProfile *chipsAt(size_t i);
const ChipProfile *chipsFind(const char *name);

// Aplica profilul in setari (mem.type, mem.size, parametri specifici).
// Pinii NU se schimba: profilul spune cum se leaga firele la pinii deja setati
// (SET pin.* ii mutam separat). err = motivul refuzului.
bool               chipsApply(const ChipProfile *p, String &err);

// Schema de conectare pentru profil, completata cu pinii CURENTI.
String             chipsWiring(const ChipProfile *p);

#endif // MEMPROG_CHIPS_H

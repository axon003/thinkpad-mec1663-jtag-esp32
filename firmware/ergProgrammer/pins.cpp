/**
 * pins.cpp — pinii din setari + detectia suprapunerilor (vezi pins.h)
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 */
#include "pins.h"
#include "cfg.h"
#include "config.h"

PinCfg g_pin;

void pinsLoad() {
    g_pin.tck  = (int8_t)cfg_int("pin.tck");
    g_pin.tms  = (int8_t)cfg_int("pin.tms");
    g_pin.tdi  = (int8_t)cfg_int("pin.tdi");
    g_pin.tdo  = (int8_t)cfg_int("pin.tdo");
    g_pin.trst = (int8_t)cfg_int("pin.trst");
    g_pin.sck  = (int8_t)cfg_int("pin.sck");
    g_pin.miso = (int8_t)cfg_int("pin.miso");
    g_pin.mosi = (int8_t)cfg_int("pin.mosi");
    g_pin.cs   = (int8_t)cfg_int("pin.cs");
    g_pin.sda  = (int8_t)cfg_int("pin.sda");
    g_pin.scl  = (int8_t)cfg_int("pin.scl");
    g_pin.mwcs = (int8_t)cfg_int("pin.mwcs");
    g_pin.mwsk = (int8_t)cfg_int("pin.mwsk");
    g_pin.mwdi = (int8_t)cfg_int("pin.mwdi");
    g_pin.mwdo = (int8_t)cfg_int("pin.mwdo");
    g_pin.stx  = (int8_t)cfg_int("pin.stx");
    g_pin.srx  = (int8_t)cfg_int("pin.srx");
    g_pin.sdtr = (int8_t)cfg_int("pin.sdtr");
    g_pin.srts = (int8_t)cfg_int("pin.srts");
    g_pin.jtagUs = (uint16_t)cfg_int("jtag.us");
    g_pin.jtagDrv = (uint8_t)cfg_int("jtag.drv");
    g_pin.spiHz  = (uint32_t)cfg_int("spi.hz");
    g_pin.i2cHz  = (uint32_t)cfg_int("i2c.hz");
}

// --- tabel pin -> lista de semnale care il folosesc -------------------------
struct PinUse { int8_t pin; const char *sig; };

static void addUse(PinUse *u, int &n, int8_t pin, const char *sig) {
    if (pin < 0) return;
    u[n].pin = pin; u[n].sig = sig; n++;
}

// Semnalele active pentru memoria curenta + cele mereu prezente (TFT, consola).
static int collectUses(const char *memtype, PinUse *u, int cap) {
    int n = 0;
    String t = memtype ? String(memtype) : String("");
    t.toLowerCase();
    if (t == "mec" || t == "mecee") {
        addUse(u, n, g_pin.tck, "JTAG TCK");   addUse(u, n, g_pin.tms, "JTAG TMS");
        addUse(u, n, g_pin.tdi, "JTAG TDI");   addUse(u, n, g_pin.tdo, "JTAG TDO");
        addUse(u, n, g_pin.trst, "JTAG TRST");
    } else if (t == "spi") {
        addUse(u, n, g_pin.sck, "SPI SCK");    addUse(u, n, g_pin.miso, "SPI MISO");
        addUse(u, n, g_pin.mosi, "SPI MOSI");  addUse(u, n, g_pin.cs, "SPI CS");
    } else if (t == "i2c") {
        addUse(u, n, g_pin.sda, "I2C SDA");    addUse(u, n, g_pin.scl, "I2C SCL");
    } else if (t == "mw") {
        addUse(u, n, g_pin.mwcs, "MW CS");     addUse(u, n, g_pin.mwsk, "MW SK");
        addUse(u, n, g_pin.mwdi, "MW DI");     addUse(u, n, g_pin.mwdo, "MW DO");
    }
    if (cfg_int("ser.on")) {
        addUse(u, n, g_pin.stx, "CONSOLA TX"); addUse(u, n, g_pin.srx, "CONSOLA RX");
        addUse(u, n, g_pin.sdtr, "CONSOLA DTR"); addUse(u, n, g_pin.srts, "CONSOLA RTS");
    }
    // TFT-urile sunt pe pini fixi si pornesc la boot daca sunt prezente.
    addUse(u, n, PIN_TFT_SCK, "TFT SCK");   addUse(u, n, PIN_TFT_MOSI, "TFT MOSI");
    addUse(u, n, PIN_TFT_MISO, "TFT MISO"); addUse(u, n, PIN_TFT_DC, "TFT DC");
    addUse(u, n, PIN_TFT_CS1, "TFT1 CS");   addUse(u, n, PIN_TFT_CS2, "TFT2 CS");
    addUse(u, n, PIN_TFT_BL, "TFT backlight");
    (void)cap;
    return n;
}

String pinsConflicts(const char *memtype) {
    PinUse u[32];
    int n = collectUses(memtype, u, 32);
    String out;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (u[i].pin != u[j].pin) continue;
            out += "CONFLICT GPIO" + String((int)u[i].pin) + ": " + u[i].sig + " si " + u[j].sig + "\n";
        }
    }
    // Capcane cunoscute pe ESP32 clasic.
    if (g_pin.tdo >= 34 || g_pin.mwdo >= 34 || g_pin.miso >= 34) { /* ok: input-only e corect pt intrari */ }
    int8_t outs[] = { g_pin.tck, g_pin.tms, g_pin.tdi, g_pin.trst, g_pin.sck, g_pin.mosi,
                      g_pin.cs, g_pin.mwcs, g_pin.mwsk, g_pin.mwdi, g_pin.stx };
    const char *names[] = { "pin.tck", "pin.tms", "pin.tdi", "pin.trst", "pin.sck", "pin.mosi",
                            "pin.cs", "pin.mwcs", "pin.mwsk", "pin.mwdi", "pin.stx" };
    for (size_t i = 0; i < sizeof(outs) / sizeof(outs[0]); i++) {
        if (outs[i] >= 34) out += String(names[i]) + "=GPIO" + String((int)outs[i]) +
                                  " este INPUT-ONLY pe ESP32 (34..39) - nu poate fi iesire\n";
        if (outs[i] == 6 || outs[i] == 7 || outs[i] == 8 || outs[i] == 9 || outs[i] == 10 || outs[i] == 11)
            out += String(names[i]) + "=GPIO" + String((int)outs[i]) + " este pe flash-ul intern - NU se foloseste\n";
    }
    return out;
}

static String pinLine(const char *sig, int8_t pin) {
    String s = "  ";
    s += sig;
    while (s.length() < 20) s += ' ';
    s += (pin < 0) ? String("-") : ("GPIO" + String((int)pin));
    s += "\n";
    return s;
}

String pinsTable() {
    String s;
    s += "JTAG (mec / mecee):\n";
    s += pinLine("TCK", g_pin.tck) + pinLine("TMS", g_pin.tms) + pinLine("TDI", g_pin.tdi) +
         pinLine("TDO (intrare)", g_pin.tdo) + pinLine("TRST", g_pin.trst);
    s += "  viteza            " + String(g_pin.jtagUs) + " us/semiperioada\n";
    s += "  putere iesire     " + String(g_pin.jtagDrv) + " (0..3)\n";
    s += "SPI NOR (spi):\n";
    s += pinLine("SCK", g_pin.sck) + pinLine("MISO", g_pin.miso) + pinLine("MOSI", g_pin.mosi) +
         pinLine("CS", g_pin.cs);
    s += "  frecventa         " + String(g_pin.spiHz) + " Hz, adresare " + (cfg_int("spi.4b") ? "4" : "3") + " bytes\n";
    s += "I2C EEPROM (i2c):\n";
    s += pinLine("SDA", g_pin.sda) + pinLine("SCL", g_pin.scl);
    s += "  adresa 7-bit      0x" + String((int)cfg_int("i2c.addr"), HEX) +
         ", adresa interna " + String(cfg_int("i2c.abytes")) + " bytes, pagina " + String(cfg_int("i2c.page")) + "\n";
    s += "Microwire (mw):\n";
    s += pinLine("CS", g_pin.mwcs) + pinLine("SK", g_pin.mwsk) + pinLine("DI (spre cip)", g_pin.mwdi) +
         pinLine("DO (de la cip)", g_pin.mwdo);
    s += "Consola serial:\n";
    s += pinLine("TX -> RX tinta", g_pin.stx) + pinLine("RX <- TX tinta", g_pin.srx) +
         pinLine("DTR", g_pin.sdtr) + pinLine("RTS", g_pin.srts);
    s += "  " + String(cfg_int("ser.baud")) + " " + cfg_str("ser.fmt") +
         (cfg_int("ser.on") ? ", pornita\n" : ", oprita\n");
    s += "TFT (fixi la compilare): SCK=" + String(PIN_TFT_SCK) + " MOSI=" + String(PIN_TFT_MOSI) +
         " MISO=" + String(PIN_TFT_MISO) + " DC=" + String(PIN_TFT_DC) +
         " CS1=" + String(PIN_TFT_CS1) + " CS2=" + String(PIN_TFT_CS2) + " BL=" + String(PIN_TFT_BL) + "\n";
    String c = pinsConflicts(cfg_str("mem.type"));
    if (c.length()) s += "\n" + c;
    return s;
}

/**
 * cfg.cpp — setarile din NVS (vezi cfg.h)
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 */
#include "cfg.h"
#include "config.h"
#include <Preferences.h>

// Ordinea de aici = ordinea afisata de CFG.
static const CfgDef DEFS[] = {
    // --- retea (aceleasi chei ca la ergCAN) ---
    {"wifi.ssid",   's', DEF_STA_SSID,       0, 0,          "retea WiFi (STA)"},
    {"wifi.pass",   'p', DEF_STA_PASS,       0, 0,          "parola WiFi"},
    {"wifi.ap",     'i', "1",                0, 1,          "AP propriu de rezerva daca STA nu prinde"},
    {"net.dns",     's', "",                 0, 0,          "DNS fix (gol = de la DHCP)"},
    {"dev.name",    's', "ergprogrammer",          0, 0,          "nume dispozitiv (prompt + mDNS)"},
    {"con.telnet",  'i', "1",                0, 1,          "shell telnet pe portul 23"},
    {"con.raw",     'i', "1",                0, 1,          "shell brut pe 2323 (host/memprog.py --tcp)"},
    {"con.pin",     'p', "",                 0, 0,          "PIN cerut la scriere/stergere (gol = fara PIN)"},

    // --- fisiere pe retea (READ/WRITE/VERIFY lucreaza cu ele) ---
    {"url.base",    's', DEF_URL_BASE,       0, 0,          "locatia de lucru, ex http://192.168.1.10:8000/"},
    {"url.put",     's', DEF_URL_PUT,        0, 0,          "script upload relativ la base (gol = PUT direct)"},
    {"url.token",   'p', "",                 0, 0,          "token trimis ca X-Token la upload/download"},

    // --- memoria pe care se lucreaza ---
    {"chip.driver",  's', "",                 0, 0,          "profil tinta (CHIPS listeaza); SET-ul afiseaza cablajul"},
    {"mem.type",    's', "mec",              0, 0,          "memoria activa: mec | mecee | spi | i2c | mw"},
    {"mem.size",    'i', "0",                0, 16777216,   "dimensiune tinta in bytes (0 = din ID/implicit)"},

    // --- JTAG / MEC16xx ---
    {"pin.tck",     'i', PIN_DEF_JTAG_TCK,  -1, 39,         "JTAG TCK"},
    {"pin.tms",     'i', PIN_DEF_JTAG_TMS,  -1, 39,         "JTAG TMS"},
    {"pin.tdi",     'i', PIN_DEF_JTAG_TDI,  -1, 39,         "JTAG TDI"},
    {"pin.tdo",     'i', PIN_DEF_JTAG_TDO,  -1, 39,         "JTAG TDO (intrare)"},
    {"pin.trst",    'i', PIN_DEF_JTAG_TRST, -1, 39,         "JTAG TRST (-1 = nefolosit)"},
    {"jtag.drv",    'i', 0,                  0, 3,          "putere iesire TCK/TMS/TDI 0..3 (0 = ~5 mA, fronturi blande pe fire lungi)"},
    {"jtag.us",     'i', DEF_JTAG_HALF_US,   0, 1000,       "semi-perioada TCK in us (2 = ~250 kHz)"},

    // --- SPI NOR ---
    {"pin.sck",     'i', PIN_DEF_SPI_SCK,   -1, 39,         "SPI SCK"},
    {"pin.miso",    'i', PIN_DEF_SPI_MISO,  -1, 39,         "SPI MISO"},
    {"pin.mosi",    'i', PIN_DEF_SPI_MOSI,  -1, 39,         "SPI MOSI"},
    {"pin.cs",      'i', PIN_DEF_SPI_CS,    -1, 39,         "SPI CS"},
    {"spi.hz",      'i', DEF_SPI_HZ,         100000, 40000000, "frecventa SPI"},
    {"spi.4b",      'i', "0",                0, 1,          "adresare pe 4 bytes (cipuri >16MB)"},

    // --- I2C EEPROM ---
    {"pin.sda",     'i', PIN_DEF_I2C_SDA,   -1, 39,         "I2C SDA"},
    {"pin.scl",     'i', PIN_DEF_I2C_SCL,   -1, 39,         "I2C SCL"},
    {"i2c.hz",      'i', DEF_I2C_HZ,         10000, 1000000, "frecventa I2C"},
    {"i2c.addr",    'i', "80",               0, 127,        "adresa 7-bit EEPROM (80 = 0x50)"},
    {"i2c.abytes",  'i', "2",                1, 2,          "adresa interna pe 1 sau 2 bytes"},
    {"i2c.page",    'i', "32",               8, 256,        "pagina de scriere (bytes)"},

    // --- Microwire 93C ---
    {"pin.mwcs",    'i', PIN_DEF_MW_CS,     -1, 39,         "Microwire CS"},
    {"pin.mwsk",    'i', PIN_DEF_MW_SK,     -1, 39,         "Microwire SK"},
    {"pin.mwdi",    'i', PIN_DEF_MW_DI,     -1, 39,         "Microwire DI (spre cip)"},
    {"pin.mwdo",    'i', PIN_DEF_MW_DO,     -1, 39,         "Microwire DO (de la cip)"},
    {"mw.abits",    'i', "6",                6, 8,          "biti adresa: 6=93C46, 7=93C56, 8=93C66"},

    // --- consola serial pentru servere/routere ---
    {"pin.stx",     'i', PIN_DEF_SER_TX,    -1, 39,         "consola: TX ESP32 -> RX tinta"},
    {"pin.srx",     'i', PIN_DEF_SER_RX,    -1, 39,         "consola: RX ESP32 <- TX tinta"},
    {"pin.sdtr",    'i', PIN_DEF_SER_DTR,   -1, 39,         "consola: DTR (-1 = nefolosit)"},
    {"pin.srts",    'i', PIN_DEF_SER_RTS,   -1, 39,         "consola: RTS (-1 = nefolosit)"},
    {"ser.baud",    'i', DEF_SER_BAUD,       300, 3000000,  "viteza consolei"},
    {"ser.fmt",     's', DEF_SER_FMT,        0, 0,          "format consola: 8N1 | 7E1 | 8E1 | 8N2 ..."},
    {"ser.on",      'i', "0",                0, 1,          "porneste consola la boot (punte pe 2324)"},
    {"ser.crlf",    'i', "0",                0, 1,          "trimite CRLF in loc de CR la Enter"},

    // --- OTA ---
    {"ota.url",     's', DEF_OTA_URL,        0, 0,          "URL versiune firmware (JSON)"},
};
#define N_DEFS (sizeof(DEFS) / sizeof(DEFS[0]))

static Preferences g_prefs;
static String      g_val[N_DEFS];

static int idx_of(const char *key) {
    for (size_t i = 0; i < N_DEFS; i++) if (strcasecmp(DEFS[i].key, key) == 0) return (int)i;
    return -1;
}

void cfg_init() {
    g_prefs.begin("memprog", false);
    for (size_t i = 0; i < N_DEFS; i++) g_val[i] = g_prefs.getString(DEFS[i].key, DEFS[i].def);
}

const char *cfg_str(const char *key) {
    int i = idx_of(key);
    return i < 0 ? "" : g_val[i].c_str();
}

long cfg_int(const char *key) {
    int i = idx_of(key);
    return i < 0 ? 0 : strtol(g_val[i].c_str(), nullptr, 10);
}

static bool is_int(const char *s) {
    if (!*s) return false;
    if (*s == '-') s++;
    if (!*s) return false;
    for (; *s; s++) if (*s < '0' || *s > '9') return false;
    return true;
}

bool cfg_set(const char *key, const char *val, String &err) {
    int i = idx_of(key);
    if (i < 0) { err = String("cheie necunoscuta: ") + key + " (CFG arata lista)"; return false; }
    const CfgDef &d = DEFS[i];
    if (d.type == 'i') {
        if (!is_int(val)) { err = String(d.key) + " cere un numar, nu \"" + val + "\""; return false; }
        long v = strtol(val, nullptr, 10);
        if (v < d.min || v > d.max) { err = String(d.key) + " trebuie intre " + d.min + " si " + d.max; return false; }
    }
    String old = g_val[i];
    g_val[i] = val;
    if (!g_prefs.putString(d.key, g_val[i])) {
        g_val[i] = old;
        err = String("nu am putut salva in NVS: ") + d.key;
        return false;
    }
    return true;
}

bool cfg_reset_key(const char *key) {
    int i = idx_of(key);
    if (i < 0) return false;
    g_val[i] = DEFS[i].def;
    g_prefs.remove(DEFS[i].key);
    return true;
}

void cfg_factory_reset() {
    for (size_t i = 0; i < N_DEFS; i++) { g_val[i] = DEFS[i].def; g_prefs.remove(DEFS[i].key); }
}

size_t        cfg_count()        { return N_DEFS; }
const CfgDef *cfg_def(size_t i)  { return i < N_DEFS ? &DEFS[i] : nullptr; }

bool cfg_is_secret(const char *key) {
    int i = idx_of(key);
    return i >= 0 && DEFS[i].type == 'p';
}

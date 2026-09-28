/**
 * cmd.cpp — dispecerul de comenzi al shell-ului (vezi cmd.h)
 *   Nivel inalt (om): HELP VER STAT CFG SET RESET FACTORY CHIPS PINS MEM ID READ WRITE VERIFY
 *                     ERASE CLEAR PIN SER OTA REBOOT
 *   Nivel jos (host/memprog.py, protocol FW 1.0 pastrat): MECID MECHALT MECSTATUS MECRD MECERASEPG
 *                     MECPROG MECEERD MECEEERASE MECEEPROG MECEEUNLOCK MECMASSERASE SPIID SPI4B SPIRD
 *                     SPIWR SPIERASE I2CCFG I2CPROBE I2CRD I2CWR MWCFG MWRD MWWR
 *   Operatiile distructive cer "YES" la final si, daca con.pin e setat, PIN <cod> inainte.
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0). Comenzile de nivel jos sunt cele din main.cpp 1.0,
 *                     cu iesirea prin console (orice owner), nu doar Serial.
 */
#include "cmd.h"
#include "config.h"
#include "cfg.h"
#include "pins.h"
#include "chips.h"
#include "console.h"
#include "netfile.h"
#include "memops.h"
#include "serbridge.h"
#include "ota.h"
#include "appstate.h"
#include "ui_display.h"
#include "jtag.h"
#include "arc_debug.h"
#include "mec16xx.h"
#include "spi_nor.h"
#include "i2c_eeprom.h"
#include "microwire93c.h"
#include <WiFi.h>

extern Jtag         jtag;
extern ArcDebug     arc;
extern Mec16xx      mec;
extern SpiNor       spiNor;
extern I2cEeprom    i2cEe;
extern Microwire93c mw;

static bool g_mecReadyLow = false;         // pt comenzile de nivel jos (ensureMec)
static bool g_unlocked[16] = {false};      // PIN dat, per owner

// ---------------------------------------------------------------------------
// raspunsuri
// ---------------------------------------------------------------------------
static void ok()                   { conPrintln("OK"); }
static void okMsg(const String &m) { conPrintln("OK " + m); }
static void err(const String &m)   { conPrintln("ERR " + m); applogErr(m); stSetResult(false, m); }
static void info(const String &m)  { conPrintln("# " + m); }
static void text(const String &multi) {           // text multi-linie (cablaj, tabele) ca linii "# "
    int start = 0;
    while (start < (int)multi.length()) {
        int nl = multi.indexOf('\n', start);
        if (nl < 0) nl = multi.length();
        conPrintln("# " + multi.substring(start, nl));
        start = nl + 1;
    }
}

// --- streaming hex "D <hex>" (protocol nivel jos) ---
static char     g_hexbuf[300];
static uint16_t g_hexlen = 0;
static const char HEXD[] = "0123456789abcdef";
static void hexFlush() {
    if (g_hexlen) {
        conWrite((const uint8_t *)"D ", 2);
        conWrite((const uint8_t *)g_hexbuf, g_hexlen);
        conWrite((const uint8_t *)"\n", 1);
        g_hexlen = 0;
    }
}
static inline void hexByte(uint8_t b) {
    g_hexbuf[g_hexlen++] = HEXD[b >> 4];
    g_hexbuf[g_hexlen++] = HEXD[b & 0xF];
    if (g_hexlen >= 256) hexFlush();
}
static bool sinkFlashWord(uint32_t w) {
    hexByte(w & 0xFF); hexByte((w >> 8) & 0xFF); hexByte((w >> 16) & 0xFF); hexByte((w >> 24) & 0xFF);
    return true;
}
static bool sinkEepromByte(uint8_t b) { hexByte(b); return true; }

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int parseHex(const char *s, uint8_t *buf, int maxlen) {
    int n = 0;
    while (s[0] && s[1]) {
        int hi = hexVal(s[0]), lo = hexVal(s[1]);
        if (hi < 0 || lo < 0) return -1;
        if (n >= maxlen) return -1;
        buf[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    if (s[0]) return -1;
    return n;
}
static uint32_t parseU32hex(const char *s) { return (uint32_t)strtoul(s, nullptr, 16); }
// nivel inalt: 0x.. hex, altfel zecimal
static bool parseNum(const char *s, uint32_t &v) {
    if (!s || !*s) return false;
    char *end;
    v = (uint32_t)strtoul(s, &end, 0);
    return *end == 0;
}
static bool isNum(const char *s) { uint32_t v; return parseNum(s, v); }

static uint8_t g_payload[4096];

static bool ensureMecLow(String &e) {
    if (g_mecReadyLow) return true;
    jtag.begin();
    ArcId id;
    if (!mec.begin(id, e)) return false;
    g_mecReadyLow = true;
    return true;
}

// destructiv: ultimul token YES + PIN daca e setat
static bool armed(int argc, char **argv, const char *what) {
    if (argc < 2 || strcasecmp(argv[argc - 1], "YES") != 0) {
        err(String(what) + " modifica memoria: adauga YES la sfarsit ca sa confirmi");
        return false;
    }
    const char *pin = cfg_str("con.pin");
    int o = conOwner();
    if (*pin && !(o >= 0 && o < 16 && g_unlocked[o])) {
        err("blocat: da intai PIN <cod> (con.pin)");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// HELP
// ---------------------------------------------------------------------------
static void help() {
    info("ergProgrammer " FW_VERSION " - comenzi (adresele: 0x hex sau zecimal)");
    info("VER | STAT | HELP | REBOOT");
    info("CFG [filtru] | SET <cheie> <val> (sau cheie=val) | RESET <cheie> | FACTORY YES");
    info("SET chip.driver=<profil>  -> aplica profilul si AFISEAZA cablajul   | CHIPS = lista profile");
    info("SET wifi.ssid/wifi.pass/wifi.ap | SET url.base <http://...> | SET pin.<x> <gpio> | PINS");
    info("MEM [mec|mecee|spi|i2c|mw]        memoria activa (= SET mem.type)");
    info("ID                                 identifica memoria activa (IDCODE MEC / JEDEC SPI / ACK I2C)");
    info("READ [addr [len]] <nume.bin>       citeste -> <url.base>/<nume.bin>  (MD5 la final)");
    info("WRITE [addr] <nume.bin> YES        <url.base>/<nume.bin> -> memorie (RMW pe blocuri, verificat)");
    info("VERIFY [addr] <nume.bin>           compara memoria cu fisierul");
    info("ERASE chip|<addr> YES              chip erase / un bloc (pagina MEC 2K, sector SPI 4K)");
    info("CLEAR <off> <len> [fill] <backup.bin> [DRY] YES   stergere chirurgicala MEC (backup pe server)");
    info("PIN <cod>                          deblocheaza scrierile daca con.pin e setat");
    info("SER ON|OFF|STAT|BREAK|DTR 0/1|RTS 0/1|LOG <nume.txt>|LOG OFF   consola seriala (telnet ip " + String(SERBRIDGE_PORT) + ")");
    info("OTA CHECK | OTA UPDATE YES         firmware de pe ota.url");
    info("nivel jos (host/memprog.py, hex fara 0x): MECID MECSTATUS MECRD <addr> <words> MECERASEPG MECPROG MECEERD");
    info("  MECEEERASE MECEEPROG MECEEUNLOCK MECMASSERASE CONFIRM | SPIID SPI4B SPIRD SPIWR SPIERASE | I2CCFG I2CPROBE I2CRD I2CWR | MWCFG MWRD MWWR");
    ok();
}

// ---------------------------------------------------------------------------
// SET (cu efecte secundare pe categorii)
// ---------------------------------------------------------------------------
static void doSet(const String &keyIn, const String &val) {
    String key = keyIn; key.toLowerCase();
    if (key == "chip.driver") {
        const ChipProfile *p = chipsFind(val.c_str());
        if (!p) { err("profil necunoscut: " + val + " (CHIPS arata lista)"); return; }
        String e;
        if (!chipsApply(p, e)) { err(e); return; }
        if (!cfg_set("chip.driver", p->name, e)) { err(e); return; }
        memReinit();
        info(String(p->name) + " - " + p->desc);
        info(String("memoria activa: ") + (strcmp(p->memtype, "ser") ? p->memtype : "(consola seriala, mem.type neschimbat)"));
        text(chipsWiring(p));
        okMsg("chip.driver=" + String(p->name));
        return;
    }
    String e;
    if (!cfg_set(key.c_str(), val.c_str(), e)) { err(e); return; }
    if (key.startsWith("pin.") || key.startsWith("spi.") || key.startsWith("i2c.") || key.startsWith("mw.") || key.startsWith("jtag.")) {
        pinsLoad(); memReinit();
        String c = pinsConflicts(cfg_str("mem.type"));
        if (c.length()) text(c);
        if (key.startsWith("pin.s") && serbridgeActive()) { String e2; if (!serbridgeStart(e2)) info("consola seriala: " + e2); }
    } else if (key.startsWith("wifi.") || key == "net.dns" || key == "dev.name") {
        info("reconectez WiFi cu setarile noi (sesiunea telnet poate cadea)");
        okMsg(key + " salvat");
        conWifiRestart();
        return;
    } else if (key.startsWith("ser.") && serbridgeActive()) {
        String e2; if (!serbridgeStart(e2)) info("consola seriala: " + e2);
    } else if (key == "mem.type") {
        if (memParse(val.c_str()) == MT_NONE) { info("valoare neasteptata pentru mem.type (mec|mecee|spi|i2c|mw)"); }
        memReinit();
    }
    okMsg(key + "=" + (cfg_is_secret(key.c_str()) ? String("****") : val));
}

static void doCfg(const char *filter) {
    for (size_t i = 0; i < cfg_count(); i++) {
        const CfgDef *d = cfg_def(i);
        if (filter && *filter && !strstr(d->key, filter)) continue;
        String v = cfg_str(d->key);
        if (d->type == 'p' && v.length()) v = "****";
        String line = d->key;
        while (line.length() < 13) line += ' ';
        line += "= " + v;
        while (line.length() < 36) line += ' ';
        line += "  " + String(d->help);
        info(line);
    }
    ok();
}

// ---------------------------------------------------------------------------
// STAT
// ---------------------------------------------------------------------------
static void doStat() {
    info(String(FW_NAME) + " " + FW_VERSION + ", uptime " + String(millis() / 1000) + " s, heap liber " + String(ESP.getFreeHeap()));
    info("WiFi: " + conWifiState() + ", ip " + conIp() + ", mac " + WiFi.macAddress());
    info("shell: telnet " + String(TELNET_PORT) + ", raw " + String(RAW_PORT) + ", serial USB " + String(SERIAL_BAUD) + " baud, mDNS " + cfg_str("dev.name") + ".local");
    info("fisiere: url.base=" + String(nfConfigured() ? cfg_str("url.base") : "(nesetat)"));
    info("memoria activa: " + String(memName(memCurrent())) + ", profil " + String(*cfg_str("chip.driver") ? cfg_str("chip.driver") : "-") +
         ", mem.size " + String(cfg_int("mem.size")));
    info("consola seriala: " + serbridgeStatus());
    info("TFT1 " + String(uiPresent(0) ? "prezent" : "absent") + ", TFT2 " + String(uiPresent(1) ? "prezent" : "absent"));
    info("OTA: " + otaZone());
    ok();
}

// ---------------------------------------------------------------------------
// nivel inalt pe memorie
// ---------------------------------------------------------------------------
static void doRead(int argc, char **argv) {
    // READ [addr [len]] <nume>
    if (argc < 2) { err("READ [addr [len]] <nume.bin>"); return; }
    String name = argv[argc - 1];
    uint32_t addr = 0, len = 0;
    if (argc >= 3 && !parseNum(argv[1], addr)) { err("adresa invalida: " + String(argv[1])); return; }
    if (argc >= 4 && !parseNum(argv[2], len)) { err("lungime invalida: " + String(argv[2])); return; }
    if (isNum(name.c_str())) { err("ultimul argument trebuie sa fie numele fisierului"); return; }
    String md5, e;
    if (!memReadToNet(addr, len, name, md5, e)) { err(e); return; }
    stSetResult(true, "READ " + name);
    okMsg(name + " scris pe " + nfUrlFor(name) + " md5 " + md5 + " (verifica cu md5sum pe server)");
}

static void doWrite(int argc, char **argv) {
    // WRITE [addr] <nume> YES
    if (!armed(argc, argv, "WRITE")) return;
    if (argc < 3) { err("WRITE [addr] <nume.bin> YES"); return; }
    String name = argv[argc - 2];
    uint32_t addr = 0;
    if (argc >= 4 && !parseNum(argv[1], addr)) { err("adresa invalida: " + String(argv[1])); return; }
    String e;
    if (!memWriteFromNet(addr, name, e)) { err(e); return; }
    stSetResult(true, "WRITE " + name);
    okMsg(name + " programat de la 0x" + String(addr, HEX) + " si verificat");
}

static void doVerify(int argc, char **argv) {
    if (argc < 2) { err("VERIFY [addr] <nume.bin>"); return; }
    String name = argv[argc - 1];
    uint32_t addr = 0;
    if (argc >= 3 && !parseNum(argv[1], addr)) { err("adresa invalida: " + String(argv[1])); return; }
    uint32_t bytes, diffs, first; String e;
    if (!memVerifyNet(addr, name, bytes, diffs, first, e)) { err(e); return; }
    if (diffs == 0) { stSetResult(true, "VERIFY OK"); okMsg("identic: " + String(bytes) + " bytes"); }
    else { err(String(diffs) + " bytes diferiti din " + String(bytes) + ", primul la 0x" + String(first, HEX)); }
}

static void doErase(int argc, char **argv) {
    if (!armed(argc, argv, "ERASE")) return;
    if (argc < 3) { err("ERASE chip|<addr> YES"); return; }
    String what = argv[1]; uint32_t addr = 0;
    if (what != "chip" && what != "all" && !parseNum(argv[1], addr)) { err("ERASE chip|<addr> YES"); return; }
    String e;
    if (!memErase(what, addr, e)) { err(e); return; }
    stSetResult(true, "ERASE");
    okMsg((what == "chip" || what == "all") ? "chip sters" : "bloc 0x" + String(addr, HEX) + " sters");
}

static void doClear(int argc, char **argv) {
    // CLEAR <off> <len> [fill] <backup.bin> [DRY] YES
    if (!armed(argc, argv, "CLEAR")) return;
    int n = argc - 1;                                  // fara YES
    bool dry = (n >= 2 && strcasecmp(argv[n - 1], "DRY") == 0);
    if (dry) n--;
    if (n < 4) { err("CLEAR <off> <len> [fill] <backup.bin> [DRY] YES"); return; }
    String backup = argv[n - 1];
    uint32_t off, len, fill = 0xFF;
    if (!parseNum(argv[1], off) || !parseNum(argv[2], len)) { err("off/len invalide"); return; }
    if (n >= 5 && !parseNum(argv[3], fill)) { err("fill invalid"); return; }
    if (fill > 0xFF) { err("fill trebuie 0..0xFF"); return; }
    String e;
    if (!memClear(off, len, (uint8_t)fill, backup, dry, e)) { err(e); return; }
    stSetResult(true, dry ? "CLEAR dry-run" : "CLEAR");
    okMsg(dry ? "dry-run: backup facut, nimic sters" : "zona stearsa; serial/MAC din restul paginii pastrate din backup");
}

static void doSer(int argc, char **argv) {
    if (argc < 2 || !strcasecmp(argv[1], "STAT")) { info(serbridgeStatus()); ok(); return; }
    String sub = argv[1]; sub.toUpperCase();
    String e;
    if (sub == "ON") {
        if (!serbridgeStart(e)) { err(e); return; }
        cfg_set("ser.on", "1", e);
        okMsg("consola pornita " + String(cfg_int("ser.baud")) + " " + cfg_str("ser.fmt") + " - conecteaza-te: telnet " + conIp() + " " + String(SERBRIDGE_PORT));
        return;
    }
    if (sub == "OFF") { serbridgeStop(); cfg_set("ser.on", "0", e); okMsg("consola oprita"); return; }
    if (sub == "BREAK") { if (!serbridgeBreak(e)) { err(e); return; } okMsg("BREAK trimis"); return; }
    if (sub == "DTR" || sub == "RTS") {
        if (argc < 3) { err("SER " + sub + " 0|1"); return; }
        if (!serbridgeSetLine(sub.c_str(), atoi(argv[2]) != 0, e)) { err(e); return; }
        okMsg(sub + "=" + String(argv[2]));
        return;
    }
    if (sub == "LOG") {
        if (argc < 3) { err("SER LOG <nume.txt> | SER LOG OFF"); return; }
        if (!strcasecmp(argv[2], "OFF")) { if (!serbridgeLogStop(e)) { err(e); return; } okMsg("log oprit"); return; }
        if (!serbridgeLogStart(argv[2], e)) { err(e); return; }
        okMsg("log -> " + nfUrlFor(argv[2]));
        return;
    }
    err("SER ON|OFF|STAT|BREAK|DTR 0/1|RTS 0/1|LOG <nume>|LOG OFF");
}

static void otaProgress(size_t done, size_t total) {
    static int last = -1;
    int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
    if (pct / 10 != last / 10) { last = pct; info("OTA " + String(pct) + "%"); stSetProgress(pct); }
    conYield();
}

static void doOta(int argc, char **argv) {
    if (argc < 2) { err("OTA CHECK | OTA UPDATE YES"); return; }
    OtaInfo i; String e;
    if (!strcasecmp(argv[1], "CHECK")) {
        if (!otaCheck(i, e)) { err(e); return; }
        info("curent " FW_VERSION ", pe server " + i.latest + (i.newer ? " (MAI NOU)" : " (nu e mai nou)") + ", " + String(i.size) + " bytes");
        if (i.notes.length()) info("note: " + i.notes);
        info("url: " + i.url);
        okMsg(i.newer ? "OTA UPDATE YES ca sa instalezi" : "esti la zi");
        return;
    }
    if (!strcasecmp(argv[1], "UPDATE")) {
        if (!armed(argc, argv, "OTA UPDATE")) return;
        if (!otaCheck(i, e)) { err(e); return; }
        if (!i.newer && !(argc >= 4 && !strcasecmp(argv[2], "FORCE"))) { err("serverul are " + i.latest + ", nu e mai nou decat " FW_VERSION " (OTA UPDATE FORCE YES ca sa rescrii)"); return; }
        info("descarc " + i.url + " (" + String(i.size) + " bytes)");
        stSetOp("OTA " + i.latest);
        if (!otaUpdate(i, e, otaProgress)) { err(e); return; }
        okMsg("firmware " + i.latest + " scris; repornesc in 1 s");
        delay(1000);
        ESP.restart();
        return;
    }
    err("OTA CHECK | OTA UPDATE [FORCE] YES");
}

// ---------------------------------------------------------------------------
// nivel jos (protocolul FW 1.0)
// ---------------------------------------------------------------------------
static bool lowLevel(int argc, char **argv) {
    String c = argv[0]; c.toUpperCase();
    String e;

    if (c == "SPIID" || c == "SPI4B" || c == "SPIRD" || c == "SPIWR" || c == "SPIERASE") spiNor.begin();
    if (c == "SPIID") {
        SpiNorId id = spiNor.readId();
        okMsg("mfg=0x" + String(id.manufacturer, HEX) + " type=0x" + String(id.memType, HEX) +
              " cap=0x" + String(id.capacity, HEX) + " size=" + String(id.sizeBytes));
        return true;
    }
    if (c == "SPI4B" && argc >= 2) { bool on = (String(argv[1]) == "on"); spiNor.set4ByteMode(on); okMsg(on ? "4-byte addressing ON" : "3-byte addressing ON"); return true; }
    if (c == "SPIRD" && argc >= 3) {
        uint32_t addr = parseU32hex(argv[1]), len = parseU32hex(argv[2]);
        uint8_t chunk[128]; uint32_t done = 0;
        while (done < len) {
            uint32_t n = (len - done > sizeof(chunk)) ? sizeof(chunk) : (len - done);
            spiNor.read(addr + done, chunk, n);
            for (uint32_t i = 0; i < n; i++) hexByte(chunk[i]);
            done += n;
            if ((done & 0x3FFF) == 0) conYield();
        }
        hexFlush(); ok(); return true;
    }
    if (c == "SPIWR" && argc >= 3) {
        uint32_t addr = parseU32hex(argv[1]);
        int n = parseHex(argv[2], g_payload, sizeof(g_payload));
        if (n < 0) { err("payload hex invalid"); return true; }
        if (spiNor.program(addr, g_payload, n, e)) okMsg("scris " + String(n) + " bytes"); else err(e);
        return true;
    }
    if (c == "SPIERASE" && argc >= 2) {
        if (String(argv[1]) == "chip") { if (spiNor.chipErase(e)) okMsg("chip erase OK"); else err(e); }
        else { uint32_t addr = parseU32hex(argv[1]); if (spiNor.sectorErase(addr, e)) okMsg("sector erase OK @0x" + String(addr, HEX)); else err(e); }
        return true;
    }
    if (c == "I2CCFG" && argc >= 4) { i2cEe.begin((uint8_t)parseU32hex(argv[1]), (uint8_t)strtoul(argv[2], nullptr, 10), (uint16_t)strtoul(argv[3], nullptr, 10)); ok(); return true; }
    if (c == "I2CPROBE") { if (i2cEe.probe()) okMsg("ACK"); else err("fara ACK"); return true; }
    if (c == "I2CRD" && argc >= 3) {
        uint32_t addr = parseU32hex(argv[1]), len = parseU32hex(argv[2]);
        uint8_t chunk[128]; uint32_t done = 0;
        while (done < len) {
            uint32_t n = (len - done > sizeof(chunk)) ? sizeof(chunk) : (len - done);
            if (!i2cEe.read(addr + done, chunk, n, e)) { hexFlush(); err(e); return true; }
            for (uint32_t i = 0; i < n; i++) hexByte(chunk[i]);
            done += n;
        }
        hexFlush(); ok(); return true;
    }
    if (c == "I2CWR" && argc >= 3) {
        uint32_t addr = parseU32hex(argv[1]);
        int n = parseHex(argv[2], g_payload, sizeof(g_payload));
        if (n < 0) { err("payload hex invalid"); return true; }
        if (i2cEe.write(addr, g_payload, n, e)) okMsg("scris " + String(n) + " bytes"); else err(e);
        return true;
    }
    if (c == "MWCFG" && argc >= 2) { mw.begin((uint8_t)strtoul(argv[1], nullptr, 10)); ok(); return true; }
    if (c == "MWRD" && argc >= 3) {
        uint16_t w = (uint16_t)parseU32hex(argv[1]); uint16_t cnt = (uint16_t)strtoul(argv[2], nullptr, 10);
        for (uint16_t i = 0; i < cnt; i++) { uint16_t v = mw.readWord(w + i); hexByte(v & 0xFF); hexByte(v >> 8); }
        hexFlush(); ok(); return true;
    }
    if (c == "MWWR" && argc >= 3) {
        uint16_t w = (uint16_t)parseU32hex(argv[1]); uint16_t v = (uint16_t)parseU32hex(argv[2]);
        mw.ewen(); bool okw = mw.writeWord(w, v); mw.ewds();
        if (okw) ok(); else err("Microwire write timeout");
        return true;
    }
    if (c == "MECID") {
        jtag.begin();
        ArcId id = arc.identify();
        String m = "IDCODE=0x" + String(id.raw, HEX) + " mfg=0x" + String(id.mfg_id, HEX) + " part=0x" + String(id.part_id, HEX) +
                   " rev=" + String(id.version) + (id.is_arc6xx ? " ARC6xx" : (id.is_arc7xx ? " ARC7xx" : " NECUNOSCUT"));
        if (id.is_arc6xx) okMsg(m); else err(m + " (astept ARC6xx)");
        return true;
    }
    if (c == "MECDIAG") {
        // Diagnostic TDI + port debug: MECID testeaza doar TCK/TMS/TDO; aici trecem date prin TDI.
        jtag.begin();
        arc.identify();                                   // Test-Logic-Reset -> RTI
        const uint32_t pat[3] = { 0x2AAAAAAA, 0x15555555, 0x0F0F1234 };
        for (int i = 0; i < 3; i++) {
            jtag.writeIR(ARC_IR_BYPASS, ARC_IR_BITS);
            uint32_t o = jtag.exchangeDR(pat[i], 32);     // BYPASS = 1 bit => o = pat<<1
            uint32_t exp = pat[i] << 1;
            info("BYPASS in=0x" + String(pat[i], HEX) + " out=0x" + String(o, HEX) + " astept=0x" + String(exp, HEX) + (o == exp ? " OK" : " GRESIT"));
        }
        const uint32_t apat[2] = { 0x12345678, 0xCAFE0001 };
        jtag.writeIR(ARC_IR_ADDRESS, ARC_IR_BITS);
        jtag.exchangeDR(apat[0], 32);
        jtag.writeIR(ARC_IR_ADDRESS, ARC_IR_BITS);
        uint32_t ra = jtag.exchangeDR(apat[1], 32);
        info("ADDRESS scris 0x" + String(apat[0], HEX) + " recitit 0x" + String(ra, HEX) + (ra == apat[0] ? " OK" : " (diferit)"));
        // fiabilitate: 50 scrieri/recitiri ADDRESS fara reset intre ele (desincronizare TAP = erori aici)
        int bad = 0; uint32_t prev = 0x5A5A0000;
        jtag.writeIR(ARC_IR_ADDRESS, ARC_IR_BITS); jtag.exchangeDR(prev, 32);
        for (int i = 1; i <= 50; i++) {
            uint32_t nx = 0x5A5A0000 ^ (i * 0x01010101u);
            jtag.writeIR(ARC_IR_ADDRESS, ARC_IR_BITS);
            uint32_t got = jtag.exchangeDR(nx, 32);
            if (got != prev) { bad++; if (bad <= 4) info("  ADR#" + String(i) + " astept 0x" + String(prev, HEX) + " primit 0x" + String(got, HEX)); }
            prev = nx;
        }
        // acelasi stres pe BYPASS (nu atinge logica ARC): daca si asta pica => semnal/fire
        int bb = 0;
        for (int i = 1; i <= 50; i++) {
            uint32_t pt = (0x1357ACE1u * i) & 0x7FFFFFFF;
            jtag.writeIR(ARC_IR_BYPASS, ARC_IR_BITS);
            if (jtag.exchangeDR(pt, 32) != (pt << 1)) bb++;
        }
        info("BYPASS stres: " + String(50 - bb) + "/50 corecte");
        info("ADDRESS fiabilitate: " + String(50 - bad) + "/50 corecte (jtag.us=" + String(g_pin.jtagUs) + " jtag.drv=" + String(g_pin.jtagDrv) + ")");
        jtag.writeIR(ARC_IR_STATUS, ARC_IR_BITS);
        info("STATUS brut inainte de tranzactie = 0x" + String(jtag.readDR(4), HEX) + " (bit0 ST, bit1 FL, bit2 RD, bit3 PC_SEL)");
        uint32_t v;
        const uint8_t aux[3] = { AUX_IDENTITY_ADDR, AUX_STATUS32_ADDR, AUX_DEBUG_ADDR };
        for (int i = 0; i < 3; i++) {
            if (arc.read(aux[i], ARC_SPACE_AUX, v, e)) info("AUX 0x" + String(aux[i], HEX) + " = 0x" + String(v, HEX));
            else info("AUX 0x" + String(aux[i], HEX) + " EROARE: " + e);
        }
        if (arc.read(0x0, ARC_SPACE_MEMORY, v, e)) info("MEM 0x0 = 0x" + String(v, HEX)); else info("MEM 0x0 EROARE: " + e);
        ok();
        return true;
    }
    if (c == "MECHALTX") {
        // EC in sleep = debug ARC fara ceas intermitent; incercam halt in bucla pana prindem o fereastra treaza.
        uint32_t secs = (argc >= 2) ? strtoul(argv[1], nullptr, 10) : 30;
        jtag.begin();
        uint32_t t0 = millis(), n = 0;
        while (millis() - t0 < secs * 1000UL) {
            n++;
            arc.identify();                               // TAP reset curat
            if (arc.setHalted(true, e)) {
                bool h1 = false, h2 = false; uint32_t idn = 0; String e2;
                bool ok1 = arc.isHalted(h1, e2), ok2 = arc.read(AUX_IDENTITY_ADDR, ARC_SPACE_AUX, idn, e2), ok3 = arc.isHalted(h2, e2);
                info("halt prins la incercarea " + String(n) + " dupa " + String((millis() - t0) / 1000.0, 1) + " s; verificare: halted=" +
                     String(ok1 && h1) + "/" + String(ok3 && h2) + " IDENTITY=0x" + String(idn, HEX) + (ok2 ? "" : " (citire esuata)"));
                if (ok1 && h1 && ok3 && h2) { okMsg("ARC HALTED"); return true; }
            }
            if ((n % 200) == 0) info("... " + String(n) + " incercari, ultima eroare: " + e);
            yield();
        }
        err("nu am prins halt in " + String(secs) + " s (" + String(n) + " incercari), ultima eroare: " + e);
        return true;
    }
    if (c == "MECHALT") { if (!ensureMecLow(e)) { err(e); return true; } if (arc.setHalted(true, e)) okMsg("halted"); else err(e); return true; }
    if (c == "MECSTATUS") {
        if (!ensureMecLow(e)) { err(e); return true; }
        uint32_t st; bool blk;
        if (!mec.readFlashStatus(st, e)) { err(e); return true; }
        if (!mec.isEepromBlocked(blk, e)) { err(e); return true; }
        okMsg("Boot_Block=" + String((st & FS_BOOT_BLOCK) ? 1 : 0) + " Data_Block=" + String((st & FS_DATA_BLOCK) ? 1 : 0) +
              " EEPROM_Block=" + String(blk ? 1 : 0) + " (flash_status=0x" + String(st, HEX) + ")");
        return true;
    }
    if (c == "MECRD" && argc >= 3) {
        if (!ensureMecLow(e)) { err(e); return true; }
        uint32_t addr = parseU32hex(argv[1]), words = parseU32hex(argv[2]);
        if (!mec.enableFlashAccess(true, e)) { err(e); return true; }
        bool okr = mec.readFlash(addr, words, sinkFlashWord, e);
        hexFlush();
        mec.enableFlashAccess(false, e);
        if (okr) ok(); else err(e);
        return true;
    }
    if (c == "MECERASEPG" && argc >= 2) {
        if (!ensureMecLow(e)) { err(e); return true; }
        uint32_t addr = parseU32hex(argv[1]);
        if (!mec.enableFlashAccess(true, e)) { err(e); return true; }
        bool oke = mec.erasePage(addr, e);
        mec.enableFlashAccess(false, e);
        if (oke) okMsg("pagina stearsa @0x" + String(addr, HEX)); else err(e);
        return true;
    }
    if (c == "MECPROG" && argc >= 3) {
        if (!ensureMecLow(e)) { err(e); return true; }
        uint32_t addr = parseU32hex(argv[1]);
        int n = parseHex(argv[2], g_payload, sizeof(g_payload));
        if (n < 0) { err("payload hex invalid"); return true; }
        if (n % 4 != 0) { err("payload trebuie multiplu de 4 bytes (words)"); return true; }
        uint32_t words = n / 4;
        uint32_t *w = (uint32_t *)g_payload;
        for (uint32_t i = 0; i < words; i++) { uint8_t *p = &g_payload[i * 4]; w[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
        if (!mec.enableFlashAccess(true, e)) { err(e); return true; }
        bool okp = mec.programFlash(addr, w, words, e);
        mec.enableFlashAccess(false, e);
        if (okp) okMsg("programat " + String(words) + " words @0x" + String(addr, HEX)); else err(e);
        return true;
    }
    if (c == "MECEERD") {
        if (!ensureMecLow(e)) { err(e); return true; }
        bool okr = mec.readEeprom(0, MEC_EEPROM_SIZE, sinkEepromByte, e);
        hexFlush();
        if (okr) ok(); else err(e);
        return true;
    }
    if (c == "MECEEERASE") { if (!ensureMecLow(e)) { err(e); return true; } if (mec.eraseEepromAll(e)) okMsg("eeprom sters"); else err(e); return true; }
    if (c == "MECEEPROG" && argc >= 3) {
        if (!ensureMecLow(e)) { err(e); return true; }
        uint32_t addr = parseU32hex(argv[1]);
        int n = parseHex(argv[2], g_payload, sizeof(g_payload));
        if (n < 0) { err("payload hex invalid"); return true; }
        if (mec.programEeprom(addr, g_payload, n, e)) okMsg("eeprom scris " + String(n) + " bytes"); else err(e);
        return true;
    }
    if (c == "MECEEUNLOCK" && argc >= 2) { if (!ensureMecLow(e)) { err(e); return true; } if (mec.unlockEeprom(parseU32hex(argv[1]), e)) okMsg("eeprom deblocat"); else err(e); return true; }
    if (c == "MECMASSERASE") {
        if (argc < 2 || String(argv[1]) != "CONFIRM") { err("MECMASSERASE sterge TOT (flash+eeprom, INCLUSIV serial/MAC). Foloseste 'MECMASSERASE CONFIRM'"); return true; }
        const char *pin = cfg_str("con.pin"); int o = conOwner();
        if (*pin && !(o >= 0 && o < 16 && g_unlocked[o])) { err("blocat: da intai PIN <cod>"); return true; }
        if (!ensureMecLow(e)) { err(e); return true; }
        info("emergency mass erase in curs (asteapta ~1s)...");
        if (mec.emergencyMassErase(e)) okMsg("mass erase rulat (poate fi necesar power-cycle)"); else err(e);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// dispecer
// ---------------------------------------------------------------------------
static void handle(int argc, char **argv) {
    if (argc == 0) return;
    String c = argv[0]; c.toUpperCase();
    String e;

    if (c == "HELP" || c == "?")      { help(); return; }
    if (c == "VER" || c == "PING")    { okMsg(String(FW_NAME) + " " + FW_VERSION + " ip " + conIp()); return; }
    if (c == "STAT")                  { doStat(); return; }
    if (c == "REBOOT")                { okMsg("repornesc"); delay(200); ESP.restart(); return; }
    if (c == "CFG")                   { doCfg(argc >= 2 ? argv[1] : nullptr); return; }
    if (c == "SET") {
        // SET cheie val | SET cheie=val | SET cheie = val
        if (argc < 2) { err("SET <cheie> <valoare>  (CFG arata cheile)"); return; }
        String k = argv[1], v;
        int eq = k.indexOf('=');
        if (eq >= 0) { v = k.substring(eq + 1); k = k.substring(0, eq); if (v.length() == 0 && argc >= 3) v = argv[2]; }
        else if (argc >= 3) { v = argv[2]; if (v == "=" && argc >= 4) v = argv[3]; }
        for (int i = (eq >= 0 ? 2 : 3); i < argc; i++) { if (String(argv[i]) == "=") continue; v += " "; v += argv[i]; }   // valori cu spatii (SSID)
        v.trim();
        if (k.length() == 0) { err("SET <cheie> <valoare>"); return; }
        doSet(k, v);
        return;
    }
    if (c == "RESET" && argc >= 2)    { if (cfg_reset_key(argv[1])) { pinsLoad(); memReinit(); okMsg(String(argv[1]) + " la implicit"); } else err("cheie necunoscuta"); return; }
    if (c == "FACTORY")               { if (argc < 2 || strcasecmp(argv[1], "YES")) { err("FACTORY YES sterge toate setarile"); return; } cfg_factory_reset(); pinsLoad(); memReinit(); okMsg("setari implicite; REBOOT"); return; }
    if (c == "CHIPS") {
        for (size_t i = 0; i < chipsCount(); i++) { const ChipProfile *p = chipsAt(i); String l = p->name; while (l.length() < 15) l += ' '; info(l + p->memtype + "   " + p->desc); }
        info("SET chip.driver=<nume> aplica profilul si afiseaza cablajul");
        ok(); return;
    }
    if (c == "PINS")                  { text(pinsTable()); if (*cfg_str("chip.driver")) { const ChipProfile *p = chipsFind(cfg_str("chip.driver")); if (p) text(chipsWiring(p)); } ok(); return; }
    if (c == "MEM") {
        if (argc < 2) { okMsg(String("memoria activa: ") + memName(memCurrent())); return; }
        if (!memSelect(argv[1], e)) { err(e); return; }
        okMsg(String("memoria activa: ") + memName(memCurrent())); return;
    }
    if (c == "ID") {
        String out;
        if (!memId(out, e)) { err(e); return; }
        stSetDevice(out);
        okMsg(out); return;
    }
    if (c == "READ")                  { doRead(argc, argv); return; }
    if (c == "WRITE")                 { doWrite(argc, argv); return; }
    if (c == "VERIFY")                { doVerify(argc, argv); return; }
    if (c == "ERASE")                 { doErase(argc, argv); return; }
    if (c == "CLEAR")                 { doClear(argc, argv); return; }
    if (c == "PIN") {
        if (argc < 2) { err("PIN <cod>"); return; }
        int o = conOwner();
        if (!*cfg_str("con.pin")) { okMsg("con.pin nu e setat - nu e nevoie de PIN"); return; }
        if (strcmp(argv[1], cfg_str("con.pin")) == 0 && o >= 0 && o < 16) { g_unlocked[o] = true; okMsg("deblocat pentru aceasta sesiune"); }
        else { delay(500); err("PIN gresit"); }
        return;
    }
    if (c == "SER")                   { doSer(argc, argv); return; }
    if (c == "OTA")                   { doOta(argc, argv); return; }
    if (c == "URL") {
        if (argc >= 2) { doSet("url.base", argv[1]); return; }
        okMsg(String("url.base=") + (nfConfigured() ? cfg_str("url.base") : "(nesetat)")); return;
    }

    if (lowLevel(argc, argv)) return;
    err("comanda necunoscuta sau argumente lipsa: " + c + " (HELP)");
}

void cmdLine(char *line) {
    char *argv[12];
    int argc = 0;
    char *p = line;
    while (*p && argc < 12) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    stClearResult();
    handle(argc, argv);
}

void cmdBegin() {
    // Driverele se initializeaza LAZY (memops::ensure / lowLevel) - pinii pot fi schimbati din shell.
}

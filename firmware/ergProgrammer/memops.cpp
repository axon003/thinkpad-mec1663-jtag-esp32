/**
 * memops.cpp — READ/WRITE/VERIFY/ERASE/CLEAR pe memoria activa (vezi memops.h)
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 */
#include "memops.h"
#include "config.h"
#include "cfg.h"
#include "pins.h"
#include "console.h"
#include "netfile.h"
#include "appstate.h"
#include "jtag.h"
#include "arc_debug.h"
#include "mec16xx.h"
#include "spi_nor.h"
#include "i2c_eeprom.h"
#include "microwire93c.h"

// Obiectele driver sunt definite in esp32_memprog.ino.
extern Jtag         jtag;
extern ArcDebug     arc;
extern Mec16xx      mec;
extern SpiNor       spiNor;
extern I2cEeprom    i2cEe;
extern Microwire93c mw;

static bool g_mecReady = false, g_spiReady = false, g_i2cReady = false, g_mwReady = false;

#define BLK_MAX 4096
static uint32_t g_blk32[BLK_MAX / 4];    // aliniat pe word (programFlash cere uint32_t*)
static uint32_t g_vfy32[BLK_MAX / 4];
static uint8_t *g_blk = (uint8_t *)g_blk32;
static uint8_t *g_vfy = (uint8_t *)g_vfy32;

// ---------------------------------------------------------------------------
// tip memorie
// ---------------------------------------------------------------------------
const char *memName(MemType t) {
    switch (t) {
    case MT_MEC:   return "mec";
    case MT_MECEE: return "mecee";
    case MT_SPI:   return "spi";
    case MT_I2C:   return "i2c";
    case MT_MW:    return "mw";
    default:       return "-";
    }
}
MemType memParse(const char *s) {
    if (!s) return MT_NONE;
    if (!strcasecmp(s, "mec"))   return MT_MEC;
    if (!strcasecmp(s, "mecee")) return MT_MECEE;
    if (!strcasecmp(s, "spi"))   return MT_SPI;
    if (!strcasecmp(s, "i2c"))   return MT_I2C;
    if (!strcasecmp(s, "mw"))    return MT_MW;
    return MT_NONE;
}
MemType memCurrent() { return memParse(cfg_str("mem.type")); }

void memReinit() { spiNor.end(); g_mecReady = g_spiReady = g_i2cReady = g_mwReady = false; }

bool memSelect(const char *name, String &err) {
    MemType t = memParse(name);
    if (t == MT_NONE) { err = String("tip necunoscut: ") + name + " (mec | mecee | spi | i2c | mw)"; return false; }
    if (!cfg_set("mem.type", memName(t), err)) return false;
    memReinit();
    return true;
}

uint32_t memBlock(MemType t) {
    switch (t) {
    case MT_MEC:   return MEC_FLASH_PAGE_SIZE;
    case MT_MECEE: return MEC_EEPROM_SIZE;      // se sterge doar integral
    case MT_SPI:   return SNOR_SECTOR_SIZE;
    case MT_MW:    return 2;                    // words, fara erase
    default:       return 1;
    }
}

// ---------------------------------------------------------------------------
// init lazy per driver (pinii pot fi schimbati intre timp -> memReinit)
// ---------------------------------------------------------------------------
static bool ensure(MemType t, String &err) {
    switch (t) {
    case MT_MEC: case MT_MECEE: {
        if (g_mecReady) return true;
        String c = pinsConflicts("mec");
        if (c.length()) conPrintln("# " + c);
        jtag.begin();
        ArcId id;
        if (!mec.begin(id, err)) return false;
        if (!id.is_arc6xx) { err = "IDCODE=0x" + String(id.raw, HEX) + " nu e ARC6xx (astept MEC16xx)"; return false; }
        g_mecReady = true;
        return true;
    }
    case MT_SPI:
        if (g_spiReady) return true;
        spiNor.begin();
        spiNor.set4ByteMode(cfg_int("spi.4b") != 0);
        g_spiReady = true;
        return true;
    case MT_I2C:
        if (g_i2cReady) return true;
        i2cEe.begin((uint8_t)cfg_int("i2c.addr"), (uint8_t)cfg_int("i2c.abytes"), (uint16_t)cfg_int("i2c.page"));
        if (!i2cEe.probe()) { err = "EEPROM I2C nu raspunde (ACK) la 0x" + String((int)cfg_int("i2c.addr"), HEX); return false; }
        g_i2cReady = true;
        return true;
    case MT_MW:
        if (g_mwReady) return true;
        mw.begin((uint8_t)cfg_int("mw.abits"));
        g_mwReady = true;
        return true;
    default:
        err = "mem.type nu e setat (SET mem.type spi | SET chip.driver=...)";
        return false;
    }
}

bool memId(String &out, String &err) {
    MemType t = memCurrent();
    if (t == MT_MEC || t == MT_MECEE) {
        jtag.begin();
        ArcId id = arc.identify();
        out = "IDCODE=0x" + String(id.raw, HEX) + " mfg=0x" + String(id.mfg_id, HEX) +
              " part=0x" + String(id.part_id, HEX) + " rev=" + String(id.version) +
              (id.is_arc6xx ? " ARC6xx (MEC16xx)" : (id.is_arc7xx ? " ARC7xx" : " NECUNOSCUT"));
        if (!id.is_arc6xx) { err = out + " - verifica firele JTAG si alimentarea tintei"; return false; }
        if (!ensure(t, err)) return false;
        uint32_t st; bool blk;
        if (!mec.readFlashStatus(st, err)) return false;
        if (!mec.isEepromBlocked(blk, err)) return false;
        out += " Boot_Block=" + String((st & FS_BOOT_BLOCK) ? 1 : 0) + " Data_Block=" + String((st & FS_DATA_BLOCK) ? 1 : 0) +
               " EEPROM_Block=" + String(blk ? 1 : 0);
        return true;
    }
    if (t == MT_SPI) {
        if (!ensure(t, err)) return false;
        SpiNorId id = spiNor.readId();
        out = "JEDEC mfg=0x" + String(id.manufacturer, HEX) + " type=0x" + String(id.memType, HEX) +
              " cap=0x" + String(id.capacity, HEX) + " size=" + String(id.sizeBytes) + " bytes";
        if (id.manufacturer == 0x00 || id.manufacturer == 0xFF) { err = out + " - cip absent, nealimentat sau fire gresite"; return false; }
        return true;
    }
    if (t == MT_I2C) {
        if (!ensure(t, err)) return false;
        out = "EEPROM I2C ACK la 0x" + String((int)cfg_int("i2c.addr"), HEX) + " (24Cxx nu are ID; dimensiunea din mem.size)";
        return true;
    }
    if (t == MT_MW) {
        if (!ensure(t, err)) return false;
        uint16_t w0 = mw.readWord(0);
        out = "Microwire 93C: word0=0x" + String(w0, HEX) + " (fara ID; dimensiunea din mw.abits)";
        return true;
    }
    err = "mem.type nu e setat";
    return false;
}

bool memSize(uint32_t &size, String &err) {
    MemType t = memCurrent();
    long s = cfg_int("mem.size");
    if (s > 0) { size = (uint32_t)s; return true; }
    switch (t) {
    case MT_MEC:   size = MEC_FLASH_SIZE_MAX; return true;
    case MT_MECEE: size = MEC_EEPROM_SIZE; return true;
    case MT_SPI: {
        if (!ensure(t, err)) return false;
        SpiNorId id = spiNor.readId();
        if (id.sizeBytes == 0) { err = "nu pot deduce dimensiunea din JEDEC ID (cap=0x" + String(id.capacity, HEX) + ") - SET mem.size"; return false; }
        size = id.sizeBytes; return true;
    }
    case MT_MW:    size = (uint32_t)2 << cfg_int("mw.abits"); return true;   // words x 2 bytes
    case MT_I2C:   err = "24Cxx nu are ID: SET mem.size <bytes> (24C256 = 32768)"; return false;
    default:       err = "mem.type nu e setat"; return false;
    }
}

// ---------------------------------------------------------------------------
// primitive pe driver
// ---------------------------------------------------------------------------
static uint8_t *g_sinkBuf; static uint32_t g_sinkIdx, g_sinkMax;
static bool sinkWord(uint32_t w) {
    if (g_sinkIdx + 4 > g_sinkMax) return false;
    g_sinkBuf[g_sinkIdx++] = w & 0xFF; g_sinkBuf[g_sinkIdx++] = (w >> 8) & 0xFF;
    g_sinkBuf[g_sinkIdx++] = (w >> 16) & 0xFF; g_sinkBuf[g_sinkIdx++] = (w >> 24) & 0xFF;
    return true;
}
static bool sinkByte(uint8_t b) {
    if (g_sinkIdx >= g_sinkMax) return false;
    g_sinkBuf[g_sinkIdx++] = b;
    return true;
}

static bool drvRead(MemType t, uint32_t addr, uint8_t *buf, uint32_t len, String &err) {
    switch (t) {
    case MT_MEC:
        if ((addr % 4) || (len % 4)) { err = "MEC: adresa si lungimea trebuie multiplu de 4"; return false; }
        g_sinkBuf = buf; g_sinkIdx = 0; g_sinkMax = len;
        if (!mec.enableFlashAccess(true, err)) return false;
        { bool ok = mec.readFlash(addr, len / 4, sinkWord, err); String e2; mec.enableFlashAccess(false, e2); if (!ok) return false; }
        if (g_sinkIdx != len) { err = "MEC: citit " + String(g_sinkIdx) + " din " + String(len); return false; }
        return true;
    case MT_MECEE:
        g_sinkBuf = buf; g_sinkIdx = 0; g_sinkMax = len;
        if (!mec.readEeprom(addr, len, sinkByte, err)) return false;
        if (g_sinkIdx != len) { err = "EEPROM: citit " + String(g_sinkIdx) + " din " + String(len); return false; }
        return true;
    case MT_SPI:
        spiNor.read(addr, buf, len);
        return true;
    case MT_I2C:
        return i2cEe.read(addr, buf, len, err);
    case MT_MW:
        if ((addr % 2) || (len % 2)) { err = "Microwire: adresa si lungimea trebuie pare (words)"; return false; }
        for (uint32_t i = 0; i < len; i += 2) { uint16_t v = mw.readWord((uint16_t)((addr + i) / 2)); buf[i] = v & 0xFF; buf[i + 1] = v >> 8; }
        return true;
    default:
        err = "mem.type nu e setat"; return false;
    }
}

static bool drvErase(MemType t, uint32_t addr, String &err) {
    switch (t) {
    case MT_MEC: {
        if (!mec.enableFlashAccess(true, err)) return false;
        bool ok = mec.erasePage(addr, err); String e2; mec.enableFlashAccess(false, e2);
        return ok;
    }
    case MT_MECEE: return mec.eraseEepromAll(err);
    case MT_SPI:   return spiNor.sectorErase(addr, err);
    default:       return true;   // I2C / MW nu au erase
    }
}

static bool drvProg(MemType t, uint32_t addr, const uint8_t *buf, uint32_t len, String &err) {
    switch (t) {
    case MT_MEC: {
        if ((addr % 4) || (len % 4) || ((uintptr_t)buf % 4)) { err = "MEC: program cere aliniere la 4"; return false; }
        if (!mec.enableFlashAccess(true, err)) return false;
        bool ok = mec.programFlash(addr, (const uint32_t *)buf, len / 4, err); String e2; mec.enableFlashAccess(false, e2);
        return ok;
    }
    case MT_MECEE: return mec.programEeprom(addr, buf, len, err);
    case MT_SPI:   return spiNor.program(addr, buf, len, err);
    case MT_I2C:   return i2cEe.write(addr, buf, len, err);
    case MT_MW: {
        mw.ewen();
        for (uint32_t i = 0; i < len; i += 2) {
            uint16_t v = buf[i] | ((uint16_t)buf[i + 1] << 8);
            if (!mw.writeWord((uint16_t)((addr + i) / 2), v)) { mw.ewds(); err = "Microwire: timeout la word " + String((addr + i) / 2); return false; }
        }
        mw.ewds();
        return true;
    }
    default: err = "mem.type nu e setat"; return false;
    }
}

static void progress(const char *what, uint32_t done, uint32_t total) {
    static int lastPct = -1;
    int pct = total ? (int)((uint64_t)done * 100 / total) : 100;
    if (done == 0) lastPct = -1;
    if (pct / 5 != lastPct / 5 || done == total) {
        lastPct = pct;
        conPrintf("# %s %u/%u (%d%%)%s", what, (unsigned)done, (unsigned)total, pct, conOwnerIsHuman() ? "\r\n" : "\n");
        stSetProgress(pct);
    }
    conYield();
}

// ---------------------------------------------------------------------------
// READ
// ---------------------------------------------------------------------------
bool memRead(uint32_t addr, uint32_t len, MemSink sink, String &err) {
    MemType t = memCurrent();
    if (!ensure(t, err)) return false;
    uint32_t chunk = (t == MT_MEC) ? 1024 : (t == MT_MW ? 64 : 1024);
    uint32_t done = 0;
    progress("citire", 0, len);
    while (done < len) {
        uint32_t n = (len - done > chunk) ? chunk : (len - done);
        if (!drvRead(t, addr + done, g_blk, n, err)) return false;
        if (!sink(g_blk, n)) { err = "destinatia a refuzat datele (client plecat / server)"; return false; }
        done += n;
        progress("citire", done, len);
    }
    return true;
}

static String g_netErr;
static bool sinkNet(const uint8_t *d, size_t n) { return nfPutWrite(d, n, g_netErr); }

bool memReadToNet(uint32_t addr, uint32_t len, const String &name, String &md5, String &err) {
    MemType t = memCurrent();
    if (!ensure(t, err)) return false;
    uint32_t size;
    if (!memSize(size, err)) return false;
    if (len == 0) len = size - addr;
    if (addr + len > size) { err = "zona 0x" + String(addr, HEX) + "+" + String(len) + " depaseste dimensiunea " + String(size); return false; }
    stSetDriver(memName(t)); stSetOp("READ -> " + name); stClearResult();
    if (!nfPutBegin(name, len, false, err)) return false;
    g_netErr = "";
    if (!memRead(addr, len, sinkNet, err)) { if (g_netErr.length()) err = g_netErr; nfPutAbort(); return false; }
    if (!nfPutEnd(md5, err)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// WRITE (read-modify-write pe blocuri) + VERIFY
// ---------------------------------------------------------------------------
static bool netFill(uint8_t *dst, uint32_t n, String &err) {
    uint32_t got = 0;
    while (got < n) {
        int r = nfGetRead(dst + got, n - got, err);
        if (r < 0) return false;
        if (r == 0) { err = "fisierul s-a terminat inainte de a umple blocul"; return false; }
        got += r;
    }
    return true;
}

bool memWriteFromNet(uint32_t addr, const String &name, String &err) {
    MemType t = memCurrent();
    if (!ensure(t, err)) return false;
    uint32_t size;
    if (!memSize(size, err)) return false;
    uint32_t fileLen;
    if (!nfGetBegin(name, 0, 0, fileLen, err)) return false;
    if (fileLen == 0) { String m; nfGetEnd(m); err = "fisier gol: " + name; return false; }
    if (addr + fileLen > size) { String m; nfGetEnd(m); err = "fisierul (" + String(fileLen) + " bytes) de la 0x" + String(addr, HEX) + " depaseste dimensiunea " + String(size); return false; }
    uint32_t blk = memBlock(t);
    bool rmw = (blk > 2);                       // MEC / MECEE / SPI: bloc de erase; I2C/MW: direct
    uint32_t step = rmw ? blk : 256;
    if (t == MT_MW && ((addr % 2) || (fileLen % 2))) { String m; nfGetEnd(m); err = "Microwire: adresa si lungimea trebuie pare"; return false; }
    if (t == MT_MEC && ((addr % 4) || (fileLen % 4))) { String m; nfGetEnd(m); err = "MEC: adresa si lungimea fisierului trebuie multiplu de 4"; return false; }
    stSetDriver(memName(t)); stSetOp("WRITE <- " + name); stClearResult();
    conPrintln("# scriu " + String(fileLen) + " bytes la 0x" + String(addr, HEX) + (rmw ? " (bloc " + String(blk) + ", read-modify-write)" : ""));
    uint32_t end = addr + fileLen;
    uint32_t b0 = rmw ? addr - (addr % blk) : addr;
    uint32_t done = 0;
    progress("scriere", 0, fileLen);
    for (uint32_t b = b0; b < end; b += step) {
        uint32_t bEnd = b + step; if (!rmw && bEnd > end) bEnd = end;
        uint32_t ovS = (addr > b) ? addr : b;
        uint32_t ovE = (end < bEnd) ? end : bEnd;
        uint32_t n = ovE - ovS;
        bool partial = rmw && (ovS != b || ovE != bEnd);
        if (partial) { if (!drvRead(t, b, g_blk, blk, err)) { String m; nfGetEnd(m); return false; } }
        if (!netFill(g_blk + (ovS - b), n, err)) { String m; nfGetEnd(m); return false; }
        uint32_t pa = rmw ? b : ovS, pl = rmw ? blk : n;
        if (rmw && !drvErase(t, b, err)) { String m; nfGetEnd(m); err = "erase bloc 0x" + String(b, HEX) + ": " + err; return false; }
        if (!drvProg(t, pa, g_blk, pl, err)) { String m; nfGetEnd(m); err = "program 0x" + String(pa, HEX) + ": " + err; return false; }
        if (!drvRead(t, pa, g_vfy, pl, err)) { String m; nfGetEnd(m); return false; }
        for (uint32_t i = 0; i < pl; i++) if (g_vfy[i] != g_blk[i]) {
            String m; nfGetEnd(m);
            err = "verificare esuata la 0x" + String(pa + i, HEX) + ": scris 0x" + String(g_blk[i], HEX) + " citit 0x" + String(g_vfy[i], HEX);
            return false;
        }
        done += n;
        progress("scriere", done, fileLen);
    }
    String md5; nfGetEnd(md5);
    conPrintln("# fisier " + name + " " + String(fileLen) + " bytes md5 " + md5);
    return true;
}

bool memVerifyNet(uint32_t addr, const String &name, uint32_t &bytes, uint32_t &diffs, uint32_t &firstDiff, String &err) {
    MemType t = memCurrent();
    if (!ensure(t, err)) return false;
    uint32_t size;
    if (!memSize(size, err)) return false;
    uint32_t fileLen;
    if (!nfGetBegin(name, 0, 0, fileLen, err)) return false;
    if (addr + fileLen > size) { String m; nfGetEnd(m); err = "fisierul depaseste dimensiunea memoriei"; return false; }
    bytes = fileLen; diffs = 0; firstDiff = 0xFFFFFFFF;
    uint32_t chunk = (t == MT_MW) ? 64 : 1024;
    uint32_t done = 0;
    progress("verificare", 0, fileLen);
    while (done < fileLen) {
        uint32_t n = (fileLen - done > chunk) ? chunk : (fileLen - done);
        if (t == MT_MEC && (n % 4)) n -= n % 4;
        if (!netFill(g_blk, n, err)) { String m; nfGetEnd(m); return false; }
        if (!drvRead(t, addr + done, g_vfy, n, err)) { String m; nfGetEnd(m); return false; }
        for (uint32_t i = 0; i < n; i++) if (g_vfy[i] != g_blk[i]) { diffs++; if (firstDiff == 0xFFFFFFFF) firstDiff = addr + done + i; }
        done += n;
        progress("verificare", done, fileLen);
    }
    String md5; nfGetEnd(md5);
    conPrintln("# fisier " + name + " md5 " + md5);
    return true;
}

// ---------------------------------------------------------------------------
// ERASE
// ---------------------------------------------------------------------------
bool memErase(const String &what, uint32_t addr, String &err) {
    MemType t = memCurrent();
    if (!ensure(t, err)) return false;
    String w = what; w.toLowerCase();
    if (w == "chip" || w == "all") {
        switch (t) {
        case MT_SPI:   return spiNor.chipErase(err);
        case MT_MECEE: return mec.eraseEepromAll(err);
        case MT_MEC:   err = "MEC: fara chip erase (ar sterge serial/MAC). ERASE <adresa> sterge o pagina; MECMASSERASE CONFIRM e ultima solutie"; return false;
        case MT_I2C: case MT_MW: {
            uint32_t size; if (!memSize(size, err)) return false;
            memset(g_blk, 0xFF, BLK_MAX);
            progress("umplere FF", 0, size);
            for (uint32_t a = 0; a < size; a += 256) {
                uint32_t n = (size - a > 256) ? 256 : size - a;
                if (!drvProg(t, a, g_blk, n, err)) return false;
                progress("umplere FF", a + n, size);
            }
            return true;
        }
        default: err = "mem.type nu e setat"; return false;
        }
    }
    uint32_t blk = memBlock(t);
    if (blk <= 2) { err = String(memName(t)) + " nu are blocuri de erase (scrie direct cu WRITE)"; return false; }
    if (addr % blk) { err = "adresa 0x" + String(addr, HEX) + " nu e la inceput de bloc (" + String(blk) + ")"; return false; }
    return drvErase(t, addr, err);
}

// ---------------------------------------------------------------------------
// CLEAR — stergere chirurgicala MEC (backup pe server, doar paginile atinse)
// ---------------------------------------------------------------------------
bool memClear(uint32_t off, uint32_t len, uint8_t fill, const String &backup, bool dryRun, String &err) {
    MemType t = memCurrent();
    if (t != MT_MEC) { err = "CLEAR e pentru mem.type=mec (flash-ul EC-ului)"; return false; }
    if ((off % 4) || (len % 4) || len == 0) { err = "offset/len trebuie multiplu de 4, len > 0"; return false; }
    if (off + len > MEC_FLASH_SIZE_MAX) { err = "zona depaseste 0x40000"; return false; }
    if (!ensure(t, err)) return false;
    uint32_t p0 = off - (off % MEC_FLASH_PAGE_SIZE);
    uint32_t p1 = (off + len - 1) - ((off + len - 1) % MEC_FLASH_PAGE_SIZE);
    uint32_t nPages = (p1 - p0) / MEC_FLASH_PAGE_SIZE + 1;
    conPrintln("# CLEAR 0x" + String(off, HEX) + " len " + String(len) + " fill 0x" + String(fill, HEX) +
               " -> " + String(nPages) + " pagina/pagini de 2048 (" + String(p0, HEX) + "..." + String(p1 + MEC_FLASH_PAGE_SIZE - 1, HEX) + ")" +
               (dryRun ? " [DRY-RUN]" : ""));

    // 1) backup integral flash + eeprom pe server
    String md5f, md5e;
    if (!memReadToNet(0, MEC_FLASH_SIZE_MAX, backup, md5f, err)) { err = "backup flash: " + err; return false; }
    conPrintln("# backup flash " + backup + " md5 " + md5f);
    {
        String eeName = backup; int dot = eeName.lastIndexOf('.'); if (dot > 0) eeName = eeName.substring(0, dot); eeName += "_eeprom.bin";
        String e2;
        if (!cfg_set("mem.type", "mecee", e2)) { err = e2; return false; }
        bool ok = memReadToNet(0, MEC_EEPROM_SIZE, eeName, md5e, err);
        cfg_set("mem.type", "mec", e2);
        if (!ok) { err = "backup eeprom: " + err; return false; }
        conPrintln("# backup eeprom " + eeName + " md5 " + md5e);
    }
    if (dryRun) { conPrintln("# dry-run: nimic sters. Verifica backup-ul pe server, apoi ruleaza fara DRY"); return true; }

    // 2) pagina cu pagina: ia pagina din backup (Range), confirma ca e identica cu flash-ul ACUM,
    //    aplica fill pe zona tinta, erase, program, verifica.
    for (uint32_t p = p0; p <= p1; p += MEC_FLASH_PAGE_SIZE) {
        uint32_t got;
        if (!nfGetBegin(backup, p, MEC_FLASH_PAGE_SIZE, got, err)) { err = "pagina 0x" + String(p, HEX) + " din backup: " + err; return false; }
        bool okFill = netFill(g_blk, MEC_FLASH_PAGE_SIZE, err);
        String m; nfGetEnd(m);
        if (!okFill) return false;
        if (!drvRead(t, p, g_vfy, MEC_FLASH_PAGE_SIZE, err)) return false;
        for (uint32_t i = 0; i < MEC_FLASH_PAGE_SIZE; i++) if (g_vfy[i] != g_blk[i]) {
            err = "backup-ul de pe server DIFERA de flash la 0x" + String(p + i, HEX) + " - NU sterg nimic (transfer corupt sau flash instabil)";
            return false;
        }
        uint32_t s = (off > p) ? off : p;
        uint32_t e = (off + len < p + MEC_FLASH_PAGE_SIZE) ? off + len : p + MEC_FLASH_PAGE_SIZE;
        memset(g_blk + (s - p), fill, e - s);
        if (!drvErase(t, p, err)) { err = "erase pagina 0x" + String(p, HEX) + ": " + err + " - REFA din " + backup; return false; }
        if (!drvProg(t, p, g_blk, MEC_FLASH_PAGE_SIZE, err)) { err = "program pagina 0x" + String(p, HEX) + ": " + err + " - REFA din " + backup; return false; }
        if (!drvRead(t, p, g_vfy, MEC_FLASH_PAGE_SIZE, err)) return false;
        for (uint32_t i = 0; i < MEC_FLASH_PAGE_SIZE; i++) if (g_vfy[i] != g_blk[i]) {
            err = "verificare pagina 0x" + String(p, HEX) + " esuata la +" + String(i) + " - REFA din " + backup;
            return false;
        }
        conPrintln("# pagina 0x" + String(p, HEX) + " rescrisa si verificata");
        conYield();
    }
    conPrintln("# zona 0x" + String(off, HEX) + "+" + String(len) + " umpluta cu 0x" + String(fill, HEX) + "; restul paginilor identic cu backup-ul");
    return true;
}

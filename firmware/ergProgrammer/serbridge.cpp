/**
 * serbridge.cpp — punte telnet <-> UART2 (vezi serbridge.h)
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 */
#include "serbridge.h"
#include "config.h"
#include "cfg.h"
#include "pins.h"
#include "netfile.h"
#include <WiFi.h>
#include <WiFiServer.h>

static WiFiServer g_srv(SERBRIDGE_PORT);
static WiFiClient g_cl[MAX_BRIDGE_CLIENTS];
static uint8_t    g_iac[MAX_BRIDGE_CLIENTS];
static bool       g_lastCr[MAX_BRIDGE_CLIENTS];
static bool       g_active = false;
static bool       g_netUp = false;
static uint32_t   g_rxBytes = 0, g_txBytes = 0;

// log pe URL: buffer + flush periodic (append)
static String   g_logName;
static uint8_t  g_logBuf[2048];
static size_t   g_logLen = 0;
static uint32_t g_logLastMs = 0;
static String   g_logErr;

// "8N1" -> config UART (baza 0x8000010: data 5..8 pe bitii 2-3, paritate pe 0-1, 2 stop = 0x20)
static bool parseFmt(const char *f, uint32_t &cfgv, String &err) {
    if (!f || strlen(f) != 3) { err = String("ser.fmt invalid: ") + (f ? f : "") + " (ex 8N1)"; return false; }
    int data = f[0] - '0';
    char par = toupper(f[1]);
    int stop = f[2] - '0';
    if (data < 5 || data > 8) { err = "ser.fmt: biti de date 5..8"; return false; }
    if (par != 'N' && par != 'E' && par != 'O') { err = "ser.fmt: paritate N/E/O"; return false; }
    if (stop != 1 && stop != 2) { err = "ser.fmt: stop 1 sau 2"; return false; }
    cfgv = 0x8000010UL | ((uint32_t)(data - 5) << 2) | (par == 'N' ? 0 : par == 'E' ? 2 : 3) | (stop == 2 ? 0x20 : 0);
    return true;
}

bool serbridgeStart(String &err) {
    if (g_pin.stx < 0 || g_pin.srx < 0) { err = "pin.stx / pin.srx nu sunt setati"; return false; }
    uint32_t fmt;
    if (!parseFmt(cfg_str("ser.fmt"), fmt, err)) return false;
    long baud = cfg_int("ser.baud");
    if (g_active) Serial2.end();
    Serial2.begin(baud, fmt, g_pin.srx, g_pin.stx);
    Serial2.setRxBufferSize(4096);
    if (g_pin.sdtr >= 0) { pinMode(g_pin.sdtr, OUTPUT); digitalWrite(g_pin.sdtr, HIGH); }
    if (g_pin.srts >= 0) { pinMode(g_pin.srts, OUTPUT); digitalWrite(g_pin.srts, HIGH); }
    g_active = true;
    g_rxBytes = g_txBytes = 0;
    return true;
}

void serbridgeStop() {
    if (!g_active) return;
    Serial2.end();
    g_active = false;
}

bool serbridgeActive() { return g_active; }

void serbridgeBegin() {
    if (cfg_int("ser.on")) {
        String e;
        if (!serbridgeStart(e)) Serial.printf("[ser] nu am putut porni consola: %s\n", e.c_str());
    }
}

void serbridgeNetUp()   { if (g_netUp) return; g_srv.begin(); g_srv.setNoDelay(true); g_netUp = true; }
void serbridgeNetDown() {
    if (!g_netUp) return;
    for (int i = 0; i < MAX_BRIDGE_CLIENTS; i++) if (g_cl[i]) g_cl[i].stop();
    g_srv.end();
    g_netUp = false;
}

bool serbridgeBreak(String &err) {
    if (!g_active) { err = "consola nu e pornita (SER ON)"; return false; }
    Serial2.flush();
    Serial2.end();
    pinMode(g_pin.stx, OUTPUT);
    digitalWrite(g_pin.stx, LOW);
    delay(250);
    digitalWrite(g_pin.stx, HIGH);
    uint32_t fmt; String e2;
    if (!parseFmt(cfg_str("ser.fmt"), fmt, e2)) { err = e2; g_active = false; return false; }
    Serial2.begin(cfg_int("ser.baud"), fmt, g_pin.srx, g_pin.stx);
    Serial2.setRxBufferSize(4096);
    return true;
}

bool serbridgeSetLine(const char *line, bool level, String &err) {
    int8_t pin = -1;
    if (strcasecmp(line, "DTR") == 0) pin = g_pin.sdtr;
    else if (strcasecmp(line, "RTS") == 0) pin = g_pin.srts;
    else { err = String("linie necunoscuta: ") + line + " (DTR sau RTS)"; return false; }
    if (pin < 0) { err = String(line) + " nu are pin (SET pin.sdtr / pin.srts)"; return false; }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level ? HIGH : LOW);
    return true;
}

// --- log pe URL ---
static bool logFlush(String &err) {
    if (g_logLen == 0) return true;
    if (!nfPutBegin(g_logName, g_logLen, true, err)) return false;
    if (!nfPutWrite(g_logBuf, g_logLen, err)) { nfPutAbort(); return false; }
    String md5;
    if (!nfPutEnd(md5, err)) return false;
    g_logLen = 0;
    g_logLastMs = millis();
    return true;
}

bool serbridgeLogStart(const String &name, String &err) {
    if (!nfConfigured()) { err = "url.base nu e setat"; return false; }
    if (name.length() == 0) { err = "lipseste numele fisierului"; return false; }
    if (g_logName.length()) { String e; logFlush(e); }
    g_logName = name; g_logLen = 0; g_logLastMs = millis(); g_logErr = "";
    // marcaj de inceput, ca sa se vada in fisier de unde porneste sesiunea
    String hdr = "\n=== ergProgrammer SER LOG start, " + String(cfg_int("ser.baud")) + " " + cfg_str("ser.fmt") + " ===\n";
    memcpy(g_logBuf, hdr.c_str(), hdr.length()); g_logLen = hdr.length();
    return logFlush(err);
}

bool serbridgeLogStop(String &err) {
    if (!g_logName.length()) { err = "nu e niciun log pornit"; return false; }
    bool ok = logFlush(err);
    g_logName = "";
    return ok;
}

static void logAdd(const uint8_t *d, size_t n) {
    if (!g_logName.length()) return;
    while (n) {
        size_t room = sizeof(g_logBuf) - g_logLen;
        size_t k = n < room ? n : room;
        memcpy(g_logBuf + g_logLen, d, k); g_logLen += k; d += k; n -= k;
        if (g_logLen == sizeof(g_logBuf)) { String e; if (!logFlush(e)) { g_logErr = e; Serial.printf("[ser] log: %s\n", e.c_str()); g_logLen = 0; } }
    }
}

// --- pompa ---
void serbridgeLoop() {
    if (g_netUp && g_srv.hasClient()) {
        int slot = -1;
        for (int i = 0; i < MAX_BRIDGE_CLIENTS; i++) if (!(g_cl[i] && g_cl[i].connected())) { slot = i; break; }
        WiFiClient nc = g_srv.available();
        if (slot < 0) { nc.write((const uint8_t *)"ocupat\r\n", 8); nc.stop(); }
        else {
            g_cl[slot] = nc; g_cl[slot].setNoDelay(true); g_iac[slot] = 0; g_lastCr[slot] = false;
            const uint8_t neg[] = {0xFF, 0xFB, 0x03, 0xFF, 0xFB, 0x01};   // WILL SGA, WILL ECHO (echo-ul il face tinta)
            g_cl[slot].write(neg, sizeof(neg));
            if (!g_active) {
                const char *m = "[ergProgrammer] consola seriala e OPRITA - din shell (port 23): SER ON\r\n";
                g_cl[slot].write((const uint8_t *)m, strlen(m));
            }
        }
    }
    // tinta -> clienti (+ log)
    if (g_active) {
        uint8_t buf[512];
        while (Serial2.available()) {
            int n = Serial2.read(buf, sizeof(buf));
            if (n <= 0) break;
            g_rxBytes += n;
            logAdd(buf, n);
            for (int i = 0; i < MAX_BRIDGE_CLIENTS; i++) {
                if (g_cl[i] && g_cl[i].connected()) {
                    // 0xFF de la tinta trebuie dublat pentru telnet
                    for (int k = 0; k < n; k++) { g_cl[i].write(buf[k]); if (buf[k] == 0xFF) g_cl[i].write((uint8_t)0xFF); }
                }
            }
        }
    }
    // clienti -> tinta
    for (int i = 0; i < MAX_BRIDGE_CLIENTS; i++) {
        WiFiClient &c = g_cl[i];
        if (!c) continue;
        if (!c.connected()) { c.stop(); continue; }
        while (c.available()) {
            int b = c.read(); if (b < 0) break;
            if (b == 0xFF) { g_iac[i] = 2; continue; }
            if (g_iac[i] > 0) { g_iac[i]--; continue; }
            if (!g_active) continue;
            // telnet trimite Enter ca CR LF sau CR NUL; tinta vrea CR (sau CRLF daca ser.crlf)
            if (g_lastCr[i] && (b == '\n' || b == 0)) { g_lastCr[i] = false; continue; }
            g_lastCr[i] = (b == '\r');
            if (b == '\r' && cfg_int("ser.crlf")) { Serial2.write("\r\n"); g_txBytes += 2; continue; }
            Serial2.write((uint8_t)b); g_txBytes++;
        }
    }
    // flush log la 2 s daca e ceva in buffer
    if (g_logName.length() && g_logLen && millis() - g_logLastMs > 2000) {
        String e; if (!logFlush(e)) { g_logErr = e; Serial.printf("[ser] log: %s\n", e.c_str()); g_logLastMs = millis(); }
    }
}

String serbridgeStatus() {
    String s = g_active ? "PORNITA " + String(cfg_int("ser.baud")) + " " + cfg_str("ser.fmt") : String("oprita");
    s += ", TX=GPIO" + String((int)g_pin.stx) + " RX=GPIO" + String((int)g_pin.srx);
    int n = 0; for (int i = 0; i < MAX_BRIDGE_CLIENTS; i++) if (g_cl[i] && g_cl[i].connected()) n++;
    s += ", port " + String(SERBRIDGE_PORT) + " clienti " + String(n);
    s += ", rx " + String(g_rxBytes) + " tx " + String(g_txBytes) + " bytes";
    if (g_logName.length()) s += ", LOG -> " + g_logName + (g_logErr.length() ? " (EROARE: " + g_logErr + ")" : "");
    return s;
}

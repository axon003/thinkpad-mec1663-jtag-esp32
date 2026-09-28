/**
 * console.cpp — WiFi (STA + AP de rezerva) si shell-ul pe serial/telnet/raw (vezi console.h)
 *   Logica WiFi si negocierea telnet sunt portate din ergissvag_can.ino v2.2.x (fix-urile
 *   de rebind dupa AP fallback, IAC/echo pt ConnectBot, keepalive).
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 */
#include "console.h"
#include "config.h"
#include "cfg.h"
#include "cmd.h"
#include "serbridge.h"
#include "ui_display.h"
#include "appstate.h"
#include <WiFi.h>
#include <WiFiServer.h>
#include <ESPmDNS.h>
#include <lwip/dns.h>
#include <stdarg.h>

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
enum WifiState { WS_STA_TRYING, WS_STA_OK, WS_AP_OK, WS_OFF };
static WifiState g_wifi = WS_OFF;
static uint32_t  g_wifiStartMs = 0;
static uint32_t  g_staDownSinceMs = 0;
static bool      g_hadSta = false;
static bool      g_apFallbackUsed = false;
static IPAddress g_lastStaIp;
static bool      g_netUp = false;

static WiFiServer g_telnetSrv(TELNET_PORT);
static WiFiServer g_rawSrv(RAW_PORT);

#define OWNER_SERIAL  0
#define OWNER_TELNET0 1
#define OWNER_RAW0    (OWNER_TELNET0 + MAX_TELNET_CLIENTS)
#define OWNER_MAX     (OWNER_RAW0 + MAX_RAW_CLIENTS)

static WiFiClient g_tc[MAX_TELNET_CLIENTS];
static char       g_tbuf[MAX_TELNET_CLIENTS][200];
static size_t     g_tlen[MAX_TELNET_CLIENTS];
static uint8_t    g_tiac[MAX_TELNET_CLIENTS];
static bool       g_tskipLf[MAX_TELNET_CLIENTS];

static WiFiClient g_rc[MAX_RAW_CLIENTS];
static char      *g_rbuf[MAX_RAW_CLIENTS];      // CMD_LINE_MAX (payload hex) - alocat la conectare
static size_t     g_rlen[MAX_RAW_CLIENTS];
static bool       g_rskipLf[MAX_RAW_CLIENTS];

static char       g_sbuf[CMD_LINE_MAX];
static size_t     g_slen = 0;

static int  g_owner = OWNER_SERIAL;
static bool g_ownerGone = false;
static bool g_busy = false;

static void applyFixedDns() {
    const char *d = cfg_str("net.dns");
    if (!d || !*d) return;
    IPAddress ip;
    if (!ip.fromString(d)) { Serial.printf("[wifi] net.dns invalid: %s\n", d); return; }
    ip_addr_t a;
    IP_ADDR4(&a, ip[0], ip[1], ip[2], ip[3]);
    dns_setserver(0, &a);
}

static void netDown() {
    if (!g_netUp) return;
    for (int i = 0; i < MAX_TELNET_CLIENTS; i++) if (g_tc[i]) g_tc[i].stop();
    for (int i = 0; i < MAX_RAW_CLIENTS; i++) if (g_rc[i]) g_rc[i].stop();
    g_telnetSrv.end();
    g_rawSrv.end();
    serbridgeNetDown();
    MDNS.end();
    g_netUp = false;
}

static void netUp() {
    if (g_netUp) return;
    const char *name = cfg_str("dev.name");
    if (!*name) name = MDNS_HOSTNAME;
    if (MDNS.begin(name)) {
        MDNS.addService("telnet", "tcp", TELNET_PORT);
        MDNS.addServiceTxt("telnet", "tcp", "fw", FW_VERSION);
    } else Serial.println("[net] mDNS nu a pornit");
    if (cfg_int("con.telnet")) { g_telnetSrv.begin(); g_telnetSrv.setNoDelay(true); }
    if (cfg_int("con.raw"))    { g_rawSrv.begin();    g_rawSrv.setNoDelay(true); }
    serbridgeNetUp();
    g_netUp = true;
}

static void staBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname(cfg_str("dev.name"));
    WiFi.begin(cfg_str("wifi.ssid"), cfg_str("wifi.pass"));
    g_wifi = WS_STA_TRYING;
    g_wifiStartMs = millis();
}

static void apBegin() {
    g_apFallbackUsed = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);
    g_wifi = WS_AP_OK;
    stSetWifi(String("AP ") + WIFI_AP_SSID, WiFi.softAPIP().toString());
    Serial.printf("[wifi] AP de rezerva %s ip %s\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    netUp();
}

void conWifiRestart() {
    netDown();
    WiFi.disconnect(true);
    g_hadSta = false; g_staDownSinceMs = 0; g_apFallbackUsed = false;
    if (strlen(cfg_str("wifi.ssid"))) staBegin();
    else apBegin();
}

static void wifiTick() {
    static uint32_t lastApRetry = 0;
    switch (g_wifi) {
    case WS_STA_TRYING:
        if (WiFi.status() == WL_CONNECTED) {
            // Dupa AP fallback rebind-ul serverelor pe interfata noua e nesigur pe ESP32 -> restart curat (ergCAN v2.1.x).
            if (g_apFallbackUsed) {
                Serial.println("[wifi] STA up dupa AP fallback -> restart pt bind curat");
                Serial.flush(); delay(200); ESP.restart();
            }
            WiFi.setSleep(false);
            IPAddress ip = WiFi.localIP();
            g_wifi = WS_STA_OK;
            applyFixedDns();
            if (!g_netUp || ip != g_lastStaIp) { netDown(); netUp(); }
            Serial.printf("[wifi] STA %s ip %s\n", cfg_str("wifi.ssid"), ip.toString().c_str());
            g_lastStaIp = ip; g_hadSta = true; g_staDownSinceMs = 0;
            stSetWifi(cfg_str("wifi.ssid"), ip.toString());
            configTime(0, 0, "pool.ntp.org", "time.google.com");   // ceas pt TLS
        } else if (millis() - g_wifiStartMs > (uint32_t)WIFI_STA_TIMEOUT_S * 1000UL) {
            uint32_t downMs  = g_staDownSinceMs ? (millis() - g_staDownSinceMs) : (millis() - g_wifiStartMs);
            uint32_t apAfter = g_hadSta ? (uint32_t)WIFI_AP_FALLBACK_S * 1000UL : (uint32_t)WIFI_STA_TIMEOUT_S * 1000UL;
            if (cfg_int("wifi.ap") && downMs >= apAfter) apBegin();
            else { g_wifiStartMs = millis(); WiFi.begin(cfg_str("wifi.ssid"), cfg_str("wifi.pass")); }
        }
        break;
    case WS_STA_OK:
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[wifi] STA pierdut, reincerc");
            g_staDownSinceMs = millis();
            g_wifi = WS_STA_TRYING; g_wifiStartMs = millis();
            WiFi.begin(cfg_str("wifi.ssid"), cfg_str("wifi.pass"));
        }
        break;
    case WS_AP_OK:
        // La 60 s incearca din nou STA (daca e configurat) fara sa darame AP-ul: scan scurt.
        if (strlen(cfg_str("wifi.ssid")) && millis() - lastApRetry > 60000UL && WiFi.softAPgetStationNum() == 0) {
            lastApRetry = millis();
            int n = WiFi.scanNetworks(false, false, false, 200);
            for (int i = 0; i < n; i++) {
                if (WiFi.SSID(i) == cfg_str("wifi.ssid")) {
                    Serial.println("[wifi] reteaua STA a reaparut -> restart pt reconectare curata");
                    Serial.flush(); delay(200); ESP.restart();
                }
            }
            WiFi.scanDelete();
        }
        break;
    case WS_OFF:
        break;
    }
}

String conIp() {
    if (g_wifi == WS_STA_OK) return WiFi.localIP().toString();
    if (g_wifi == WS_AP_OK) return WiFi.softAPIP().toString();
    return String("0.0.0.0");
}
String conWifiState() {
    switch (g_wifi) {
    case WS_STA_TRYING: return String("STA se conecteaza la ") + cfg_str("wifi.ssid");
    case WS_STA_OK:     return String("STA ") + cfg_str("wifi.ssid") + " rssi " + String(WiFi.RSSI()) + " dBm";
    case WS_AP_OK:      return String("AP de rezerva ") + WIFI_AP_SSID + " (" + String(WiFi.softAPgetStationNum()) + " clienti)";
    default:              return String("oprit");
    }
}
bool conNetUp() { return g_netUp; }

// ---------------------------------------------------------------------------
// iesire catre owner
// ---------------------------------------------------------------------------
static WiFiClient *ownerClient(int o) {
    if (o >= OWNER_TELNET0 && o < OWNER_TELNET0 + MAX_TELNET_CLIENTS) return &g_tc[o - OWNER_TELNET0];
    if (o >= OWNER_RAW0 && o < OWNER_RAW0 + MAX_RAW_CLIENTS) return &g_rc[o - OWNER_RAW0];
    return nullptr;
}

int  conOwner() { return g_owner; }
bool conOwnerIsHuman() { return g_owner >= OWNER_TELNET0 && g_owner < OWNER_RAW0; }

bool conWrite(const uint8_t *data, size_t n) {
    if (g_ownerGone) return false;
    if (g_owner == OWNER_SERIAL) { Serial.write(data, n); return true; }
    WiFiClient *c = ownerClient(g_owner);
    if (!c || !c->connected()) { g_ownerGone = true; return false; }
    size_t done = 0; uint32_t t0 = millis();
    while (done < n) {
        size_t w = c->write(data + done, n - done);
        if (w == 0) {
            if (!c->connected() || millis() - t0 > 10000) { g_ownerGone = true; return false; }
            delay(1); continue;
        }
        t0 = millis(); done += w;
    }
    return true;
}
bool conPrint(const String &s) { return conWrite((const uint8_t *)s.c_str(), s.length()); }
bool conPrintln(const String &s) {
    if (!conPrint(s)) return false;
    return conWrite((const uint8_t *)(conOwnerIsHuman() ? "\r\n" : "\n"), conOwnerIsHuman() ? 2 : 1);
}
bool conPrintf(const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    return conWrite((const uint8_t *)buf, n);
}

void conYield() {
    uiTick();
    if (g_owner != OWNER_SERIAL) {
        WiFiClient *c = ownerClient(g_owner);
        if (!c || !c->connected()) g_ownerGone = true;
    }
    delay(0);
}

// ---------------------------------------------------------------------------
// dispecer
// ---------------------------------------------------------------------------
static void runLine(int owner, char *line) {
    if (g_busy) {
        int save = g_owner; g_owner = owner; g_ownerGone = false;
        conPrintln("ERR ocupat: alta comanda ruleaza (asteapta sa se termine)");
        g_owner = save;
        return;
    }
    g_busy = true;
    g_owner = owner; g_ownerGone = false;
    cmdLine(line);
    g_busy = false;
    g_owner = OWNER_SERIAL;
}

static void prompt(WiFiClient &c) {
    String p = String(cfg_str("dev.name")) + "> ";
    c.write((const uint8_t *)p.c_str(), p.length());
}

// ---------------------------------------------------------------------------
// telnet (om)
// ---------------------------------------------------------------------------
static void serviceTelnet() {
    if (cfg_int("con.telnet") && g_telnetSrv.hasClient()) {
        int slot = -1;
        for (int i = 0; i < MAX_TELNET_CLIENTS; i++) if (!(g_tc[i] && g_tc[i].connected())) { slot = i; break; }
        WiFiClient nc = g_telnetSrv.available();
        if (slot < 0) { nc.write((const uint8_t *)"ocupat: prea multi clienti\r\n", 28); nc.stop(); }
        else {
            g_tc[slot] = nc; g_tc[slot].setNoDelay(true); g_tlen[slot] = 0; g_tiac[slot] = 0; g_tskipLf[slot] = false;
            // negociere: serverul face echo si suprima go-ahead (char mode) - altfel ConnectBot nu arata nimic
            const uint8_t neg[] = {0xFF, 0xFB, 0x01, 0xFF, 0xFB, 0x03};   // IAC WILL ECHO, IAC WILL SGA
            g_tc[slot].write(neg, sizeof(neg));
            String w = String(FW_NAME) + " " + FW_VERSION + " - HELP pentru comenzi, CHIPS pentru profile de tinta\r\n";
            g_tc[slot].write((const uint8_t *)w.c_str(), w.length());
            prompt(g_tc[slot]);
        }
    }
    for (int i = 0; i < MAX_TELNET_CLIENTS; i++) {
        WiFiClient &c = g_tc[i];
        if (!c) continue;
        if (!c.connected()) { c.stop(); continue; }
        while (c.available()) {
            int b = c.read(); if (b < 0) break;
            if (b == 0xFF) { g_tiac[i] = 2; continue; }          // IAC + 2 octeti
            if (g_tiac[i] > 0) { g_tiac[i]--; continue; }
            if (b >= 0x80 || b == 0) continue;
            if (b == '\b' || b == 0x7F) {
                if (g_tlen[i] > 0) { g_tlen[i]--; c.write((const uint8_t *)"\b \b", 3); }
                continue;
            }
            if (b == '\n' && g_tskipLf[i]) { g_tskipLf[i] = false; continue; }
            if (b == '\r' || b == '\n') {
                g_tskipLf[i] = (b == '\r');
                c.write((const uint8_t *)"\r\n", 2);
                if (g_tlen[i] > 0) { g_tbuf[i][g_tlen[i]] = 0; runLine(OWNER_TELNET0 + i, g_tbuf[i]); g_tlen[i] = 0; }
                if (c.connected()) prompt(c);
                continue;
            }
            g_tskipLf[i] = false;
            if (b >= 0x20 && g_tlen[i] < sizeof(g_tbuf[i]) - 1) { g_tbuf[i][g_tlen[i]++] = (char)b; c.write((uint8_t *)&b, 1); }
        }
    }
}

// ---------------------------------------------------------------------------
// raw (host/memprog.py --tcp): linii terminate cu \n, fara echo, fara IAC
// ---------------------------------------------------------------------------
static void serviceRaw() {
    if (cfg_int("con.raw") && g_rawSrv.hasClient()) {
        int slot = -1;
        for (int i = 0; i < MAX_RAW_CLIENTS; i++) if (!(g_rc[i] && g_rc[i].connected())) { slot = i; break; }
        WiFiClient nc = g_rawSrv.available();
        if (slot < 0) { nc.write((const uint8_t *)"ERR ocupat\n", 11); nc.stop(); }
        else {
            if (!g_rbuf[slot]) g_rbuf[slot] = (char *)malloc(CMD_LINE_MAX);
            if (!g_rbuf[slot]) { nc.write((const uint8_t *)"ERR fara memorie pentru buffer\n", 31); nc.stop(); return; }
            g_rc[slot] = nc; g_rc[slot].setNoDelay(true); g_rlen[slot] = 0; g_rskipLf[slot] = false;
            String w = String("# ") + FW_NAME + " " + FW_VERSION + " raw\n";
            g_rc[slot].write((const uint8_t *)w.c_str(), w.length());
        }
    }
    for (int i = 0; i < MAX_RAW_CLIENTS; i++) {
        WiFiClient &c = g_rc[i];
        if (!c) continue;
        if (!c.connected()) { c.stop(); continue; }
        while (c.available()) {
            int b = c.read(); if (b < 0) break;
            if (b == '\n' && g_rskipLf[i]) { g_rskipLf[i] = false; continue; }
            g_rskipLf[i] = (b == '\r');
            if (b == '\r' || b == '\n') {
                g_rbuf[i][g_rlen[i]] = 0;
                if (g_rlen[i] > 0) runLine(OWNER_RAW0 + i, g_rbuf[i]);
                g_rlen[i] = 0;
            } else if (g_rlen[i] < CMD_LINE_MAX - 1) g_rbuf[i][g_rlen[i]++] = (char)b;
            else {
                g_rlen[i] = 0;
                c.write((const uint8_t *)"ERR linie prea lunga (>CMD_LINE_MAX)\n", 37);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// serial USB
// ---------------------------------------------------------------------------
static void serviceSerial() {
    static bool skipLf = false;
    while (Serial.available()) {
        char ch = (char)Serial.read();
        // PuTTY trimite CR la Enter, host/memprog.py trimite LF: oricare inchide linia; LF-ul de
        // dupa un CR (CRLF) se ignora ca sa nu ruleze o linie goala.
        if (ch == '\n' && skipLf) { skipLf = false; continue; }
        skipLf = (ch == '\r');
        if (ch == '\r' || ch == '\n') {
            g_sbuf[g_slen] = 0;
            if (g_slen > 0) runLine(OWNER_SERIAL, g_sbuf);
            g_slen = 0;
        } else if (g_slen < CMD_LINE_MAX - 1) g_sbuf[g_slen++] = ch;
        else { g_slen = 0; Serial.print("ERR linie prea lunga (>CMD_LINE_MAX)\n"); }
    }
}

// ---------------------------------------------------------------------------
void conBegin() {
    for (int i = 0; i < MAX_RAW_CLIENTS; i++) g_rbuf[i] = nullptr;
    if (strlen(cfg_str("wifi.ssid"))) staBegin();
    else apBegin();
}

void conLoop() {
    wifiTick();
    if (g_netUp) { serviceTelnet(); serviceRaw(); serbridgeLoop(); }
    serviceSerial();
}

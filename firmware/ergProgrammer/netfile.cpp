/**
 * netfile.cpp — HTTP in flux pentru fisiere pe retea (vezi netfile.h)
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 */
#include "netfile.h"
#include "cfg.h"
#include "ca_isrg.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "mbedtls/md5.h"

#define NF_TIMEOUT_MS 15000

// ---------------------------------------------------------------------------
// URL
// ---------------------------------------------------------------------------
struct UrlParts { bool https; String host; uint16_t port; String path; };

static bool parseUrl(const String &url, UrlParts &u, String &err) {
    String s = url;
    if (s.startsWith("https://")) { u.https = true; s = s.substring(8); u.port = 443; }
    else if (s.startsWith("http://")) { u.https = false; s = s.substring(7); u.port = 80; }
    else { err = "URL fara http:// sau https://: " + url; return false; }
    int slash = s.indexOf('/');
    String hp = slash < 0 ? s : s.substring(0, slash);
    u.path = slash < 0 ? String("/") : s.substring(slash);
    int colon = hp.indexOf(':');
    if (colon >= 0) { u.host = hp.substring(0, colon); u.port = (uint16_t)hp.substring(colon + 1).toInt(); }
    else u.host = hp;
    if (u.host.length() == 0) { err = "URL fara host: " + url; return false; }
    return true;
}

bool nfConfigured() { return strlen(cfg_str("url.base")) > 0; }

static String baseSlash() {
    String b = cfg_str("url.base");
    if (b.length() && !b.endsWith("/")) b += "/";
    return b;
}

String nfUrlFor(const String &name) {
    if (!nfConfigured()) return String();
    return baseSlash() + name;
}

// ---------------------------------------------------------------------------
// conexiune + antet
// ---------------------------------------------------------------------------
static WiFiClient       g_plain;
static WiFiClientSecure g_tls;
static WiFiClient      *g_c = nullptr;
static bool             g_secureInit = false;

static WiFiClient *connectTo(const UrlParts &u, String &err) {
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP) { err = "WiFi neconectat"; return nullptr; }
    WiFiClient *c;
    if (u.https) {
        if (!g_secureInit) { g_tls.setCACert(CA_ISRG_ROOT_X1); g_secureInit = true; }
        c = &g_tls;
    } else c = &g_plain;
    c->setTimeout(NF_TIMEOUT_MS / 1000);
    if (!c->connect(u.host.c_str(), u.port, NF_TIMEOUT_MS)) {
        err = "nu ma pot conecta la " + u.host + ":" + String(u.port) + (u.https ? " (TLS)" : "");
        return nullptr;
    }
    c->setNoDelay(true);
    return c;
}

static void sendHead(WiFiClient *c, const char *method, const UrlParts &u, uint32_t contentLen,
                     bool hasBody, const String &extra) {
    String h = String(method) + " " + u.path + " HTTP/1.1\r\n";
    h += "Host: " + u.host + "\r\n";
    h += "User-Agent: esp32_memprog\r\n";
    h += "Connection: close\r\n";
    const char *tok = cfg_str("url.token");
    if (*tok) h += String("X-Token: ") + tok + "\r\n";
    if (hasBody) {
        h += "Content-Type: application/octet-stream\r\n";
        h += "Content-Length: " + String(contentLen) + "\r\n";   // cunoscut dinainte => fara chunked
    }
    h += extra;
    h += "\r\n";
    c->print(h);
}

// Citeste linia de status + antetele. Intoarce codul HTTP, umple contentLen (-1 daca lipseste)
// si chunked. Timeout explicit.
static int readHead(WiFiClient *c, long &contentLen, bool &chunked, String &err) {
    contentLen = -1; chunked = false;
    uint32_t t0 = millis();
    while (!c->available()) {
        if (!c->connected() && !c->available()) { err = "serverul a inchis conexiunea inainte de raspuns"; return -1; }
        if (millis() - t0 > NF_TIMEOUT_MS) { err = "timeout asteptand raspunsul HTTP"; return -1; }
        delay(1);
    }
    String status = c->readStringUntil('\n');
    status.trim();
    if (!status.startsWith("HTTP/")) { err = "raspuns HTTP invalid: " + status; return -1; }
    int sp = status.indexOf(' ');
    int code = status.substring(sp + 1, sp + 4).toInt();
    for (;;) {
        String l = c->readStringUntil('\n');
        l.trim();
        if (l.length() == 0) break;
        String low = l; low.toLowerCase();
        if (low.startsWith("content-length:")) contentLen = l.substring(15).toInt();
        else if (low.startsWith("transfer-encoding:") && low.indexOf("chunked") > 0) chunked = true;
    }
    if (code == 0) { err = "cod HTTP lipsa in: " + status; return -1; }
    return code;
}

static String md5hex(unsigned char d[16]) {
    static const char H[] = "0123456789abcdef";
    String s;
    for (int i = 0; i < 16; i++) { s += H[d[i] >> 4]; s += H[d[i] & 0xF]; }
    return s;
}

// ---------------------------------------------------------------------------
// upload in flux
// ---------------------------------------------------------------------------
static mbedtls_md5_context g_putMd5;
static uint32_t g_putLeft = 0;
static bool     g_putOpen = false;

bool nfPutBegin(const String &name, uint32_t total, bool append, String &err) {
    if (g_putOpen) nfPutAbort();
    if (!nfConfigured()) { err = "url.base nu e setat (SET url.base http://...)"; return false; }
    if (name.length() == 0 || name.indexOf('/') >= 0 || name.indexOf("..") >= 0) { err = "nume fisier invalid: " + name; return false; }
    const char *put = cfg_str("url.put");
    String url; const char *method;
    if (*put) {
        url = (strncmp(put, "http", 4) == 0) ? String(put) : baseSlash() + put;   // absolut sau relativ la base
        url += "?f=" + name + (append ? "&append=1" : "");
        method = "POST";
    }
    else      { url = baseSlash() + name; method = "PUT"; if (append) { err = "append cere url.put (PUT direct nu stie append)"; return false; } }
    UrlParts u;
    if (!parseUrl(url, u, err)) return false;
    g_c = connectTo(u, err);
    if (!g_c) return false;
    sendHead(g_c, method, u, total, true, String());
    mbedtls_md5_init(&g_putMd5);
    mbedtls_md5_starts(&g_putMd5);
    g_putLeft = total; g_putOpen = true;
    return true;
}

bool nfPutWrite(const uint8_t *data, size_t n, String &err) {
    if (!g_putOpen) { err = "upload nedeschis"; return false; }
    if (n > g_putLeft) { err = "upload: mai multi bytes decat Content-Length anuntat"; return false; }
    size_t done = 0;
    uint32_t t0 = millis();
    while (done < n) {
        if (!g_c->connected()) { err = "serverul a inchis conexiunea in timpul upload-ului"; return false; }
        size_t w = g_c->write(data + done, n - done);
        if (w == 0) {
            if (millis() - t0 > NF_TIMEOUT_MS) { err = "timeout la scriere (upload)"; return false; }
            delay(1);
            continue;
        }
        t0 = millis();
        done += w;
    }
    mbedtls_md5_update(&g_putMd5, data, n);
    g_putLeft -= n;
    return true;
}

bool nfPutEnd(String &md5, String &err) {
    if (!g_putOpen) { err = "upload nedeschis"; return false; }
    unsigned char d[16];
    mbedtls_md5_finish(&g_putMd5, d);
    mbedtls_md5_free(&g_putMd5);
    md5 = md5hex(d);
    bool ok = true;
    if (g_putLeft != 0) { err = "upload incomplet: lipsesc " + String(g_putLeft) + " bytes"; ok = false; }
    else {
        long cl; bool ch;
        int code = readHead(g_c, cl, ch, err);
        if (code < 0) ok = false;
        else if (code < 200 || code >= 300) {
            String body; uint32_t t0 = millis();
            while ((g_c->connected() || g_c->available()) && body.length() < 300 && millis() - t0 < 3000) {
                while (g_c->available() && body.length() < 300) body += (char)g_c->read();
                delay(1);
            }
            body.trim();
            err = "serverul a raspuns HTTP " + String(code) + (body.length() ? ": " + body : String());
            ok = false;
        }
    }
    g_c->stop(); g_c = nullptr; g_putOpen = false;
    return ok;
}

void nfPutAbort() {
    if (!g_putOpen) return;
    mbedtls_md5_free(&g_putMd5);
    if (g_c) { g_c->stop(); g_c = nullptr; }
    g_putOpen = false;
}

// ---------------------------------------------------------------------------
// download in flux
// ---------------------------------------------------------------------------
static mbedtls_md5_context g_getMd5;
static long g_getLeft = 0;
static bool g_getOpen = false;

bool nfGetBegin(const String &name, uint32_t off, uint32_t len, uint32_t &contentLen, String &err) {
    if (g_getOpen) { String m; nfGetEnd(m); }
    bool absolute = name.startsWith("http://") || name.startsWith("https://");   // OTA da URL absolut
    if (!absolute && !nfConfigured()) { err = "url.base nu e setat (SET url.base http://...)"; return false; }
    if (name.length() == 0 || (!absolute && name.indexOf("..") >= 0)) { err = "nume fisier invalid: " + name; return false; }
    UrlParts u;
    if (!parseUrl(absolute ? name : baseSlash() + name, u, err)) return false;
    g_c = connectTo(u, err);
    if (!g_c) return false;
    String extra;
    bool ranged = (off > 0 || len > 0);
    if (ranged) {
        extra = "Range: bytes=" + String(off) + "-";
        if (len > 0) extra += String(off + len - 1);
        extra += "\r\n";
    }
    sendHead(g_c, "GET", u, 0, false, extra);
    long cl; bool chunked;
    int code = readHead(g_c, cl, chunked, err);
    if (code < 0) { g_c->stop(); g_c = nullptr; return false; }
    if (code == 404) { err = "fisierul nu exista pe server: " + name; g_c->stop(); g_c = nullptr; return false; }
    if (ranged && code != 206) {
        err = "serverul nu suporta Range (a raspuns " + String(code) + " in loc de 206) - foloseste host/serve_files.py sau Apache";
        g_c->stop(); g_c = nullptr; return false;
    }
    if (code < 200 || code >= 300) { err = "serverul a raspuns HTTP " + String(code) + " pentru " + name; g_c->stop(); g_c = nullptr; return false; }
    if (chunked || cl < 0) {
        err = "raspuns fara Content-Length (chunked) - nu pot verifica dimensiunea; serveste fisierul static";
        g_c->stop(); g_c = nullptr; return false;
    }
    if (ranged && len > 0 && (uint32_t)cl != len) {
        err = "Range: am cerut " + String(len) + " bytes, serverul da " + String(cl);
        g_c->stop(); g_c = nullptr; return false;
    }
    contentLen = (uint32_t)cl;
    g_getLeft = cl;
    mbedtls_md5_init(&g_getMd5);
    mbedtls_md5_starts(&g_getMd5);
    g_getOpen = true;
    return true;
}

int nfGetRead(uint8_t *buf, size_t n, String &err) {
    if (!g_getOpen) { err = "download nedeschis"; return -1; }
    if (g_getLeft <= 0) return 0;
    if ((long)n > g_getLeft) n = (size_t)g_getLeft;
    uint32_t t0 = millis();
    while (!g_c->available()) {
        if (!g_c->connected()) { err = "conexiunea s-a inchis cu " + String(g_getLeft) + " bytes neprimiti"; return -1; }
        if (millis() - t0 > NF_TIMEOUT_MS) { err = "timeout la citire (download)"; return -1; }
        delay(1);
    }
    int r = g_c->read(buf, n);
    if (r <= 0) { err = "eroare de citire din socket"; return -1; }
    mbedtls_md5_update(&g_getMd5, buf, r);
    g_getLeft -= r;
    return r;
}

void nfGetEnd(String &md5) {
    if (!g_getOpen) { md5 = ""; return; }
    unsigned char d[16];
    mbedtls_md5_finish(&g_getMd5, d);
    mbedtls_md5_free(&g_getMd5);
    md5 = md5hex(d);
    if (g_c) { g_c->stop(); g_c = nullptr; }
    g_getOpen = false;
}

// ---------------------------------------------------------------------------
// GET / POST mici
// ---------------------------------------------------------------------------
static bool readBodyTo(WiFiClient *c, long cl, String &body, size_t maxLen, String &err) {
    uint32_t t0 = millis();
    while (c->connected() || c->available()) {
        while (c->available()) {
            char ch = (char)c->read();
            if (body.length() < maxLen) body += ch;
            t0 = millis();
        }
        if (cl >= 0 && (long)body.length() >= cl) break;
        if (millis() - t0 > NF_TIMEOUT_MS) { err = "timeout la citirea body-ului"; return false; }
        delay(1);
    }
    return true;
}

bool nfGetSmall(const String &url, String &body, String &err, size_t maxLen) {
    UrlParts u;
    if (!parseUrl(url, u, err)) return false;
    WiFiClient *c = connectTo(u, err);
    if (!c) return false;
    sendHead(c, "GET", u, 0, false, String());
    long cl; bool chunked;
    int code = readHead(c, cl, chunked, err);
    if (code < 0) { c->stop(); return false; }
    body = "";
    bool ok = readBodyTo(c, chunked ? -1 : cl, body, maxLen, err);
    c->stop();
    if (!ok) return false;
    if (code < 200 || code >= 300) { err = "HTTP " + String(code) + " pentru " + url + (body.length() ? ": " + body.substring(0, 120) : String()); return false; }
    return true;
}

bool nfPostSmall(const String &url, const String &payload, const char *contentType, String &resp, String &err) {
    UrlParts u;
    if (!parseUrl(url, u, err)) return false;
    WiFiClient *c = connectTo(u, err);
    if (!c) return false;
    String h = "POST " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nUser-Agent: esp32_memprog\r\nConnection: close\r\n";
    const char *tok = cfg_str("url.token");
    if (*tok) h += String("X-Token: ") + tok + "\r\n";
    h += String("Content-Type: ") + contentType + "\r\nContent-Length: " + String(payload.length()) + "\r\n\r\n";
    c->print(h);
    c->print(payload);
    long cl; bool chunked;
    int code = readHead(c, cl, chunked, err);
    if (code < 0) { c->stop(); return false; }
    resp = "";
    bool ok = readBodyTo(c, chunked ? -1 : cl, resp, 512, err);
    c->stop();
    if (!ok) return false;
    if (code < 200 || code >= 300) { err = "HTTP " + String(code) + (resp.length() ? ": " + resp.substring(0, 120) : String()); return false; }
    return true;
}

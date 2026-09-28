/**
 * ota.cpp — OTA CHECK / UPDATE (vezi ota.h)
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 */
#include "ota.h"
#include "config.h"
#include "cfg.h"
#include "netfile.h"
#include <WiFi.h>
#include <Update.h>
#include "esp_ota_ops.h"

// extrage "key":"valoare" sau "key":numar dintr-un JSON plat (fara librarie)
static String jsonField(const String &body, const char *key) {
    String k = String("\"") + key + "\"";
    int i = body.indexOf(k);
    if (i < 0) return String();
    i = body.indexOf(':', i + k.length());
    if (i < 0) return String();
    i++;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) i++;
    if (i >= (int)body.length()) return String();
    if (body[i] == '"') {
        int j = body.indexOf('"', i + 1);
        return j < 0 ? String() : body.substring(i + 1, j);
    }
    int j = i;
    while (j < (int)body.length() && body[j] != ',' && body[j] != '}' && body[j] != ' ') j++;
    return body.substring(i, j);
}

int otaVersionCmp(const char *a, const char *b) {
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!*a && !*b) break;
        if (!isdigit((unsigned char)*a) && *a) a++;
        if (!isdigit((unsigned char)*b) && *b) b++;
    }
    return 0;
}

bool otaCheck(OtaInfo &info, String &err) {
    const char *base = cfg_str("ota.url");
    if (!*base) { err = "ota.url nu e setat"; return false; }
    String url = String(base) + (strchr(base, '?') ? "&" : "?") + "fw=" FW_VERSION "&dev=" + WiFi.macAddress();
    String body;
    if (!nfGetSmall(url, body, err)) return false;
    info.latest = jsonField(body, "fw");
    info.url    = jsonField(body, "url");
    info.md5    = jsonField(body, "md5");
    info.notes  = jsonField(body, "notes");
    info.size   = (uint32_t)jsonField(body, "size").toInt();
    if (info.latest.length() == 0 || info.url.length() == 0) { err = "raspuns fara fw/url: " + body.substring(0, 160); return false; }
    info.newer = otaVersionCmp(info.latest.c_str(), FW_VERSION) > 0;
    return true;
}

bool otaUpdate(const OtaInfo &info, String &err, OtaProgress progress) {
    if (!info.url.startsWith("http")) { err = "URL firmware invalid: " + info.url; return false; }
    uint32_t total;
    if (!nfGetBegin(info.url, 0, 0, total, err)) return false;
    if (total < 100000) { String m; nfGetEnd(m); err = "fisier prea mic pentru un firmware: " + String(total) + " bytes"; return false; }
    if (info.size && info.size != total) { String m; nfGetEnd(m); err = "dimensiune diferita: server anunta " + String(info.size) + ", Content-Length " + String(total); return false; }
    if (!Update.begin(total, U_FLASH)) { String m; nfGetEnd(m); err = "Update.begin: " + String(Update.errorString()); return false; }
    if (info.md5.length() == 32) Update.setMD5(info.md5.c_str());
    uint8_t buf[1024];
    size_t done = 0;
    for (;;) {
        int n = nfGetRead(buf, sizeof(buf), err);
        if (n < 0) { Update.abort(); String m; nfGetEnd(m); return false; }
        if (n == 0) break;
        if (Update.write(buf, n) != (size_t)n) { err = "scriere flash: " + String(Update.errorString()); Update.abort(); String m; nfGetEnd(m); return false; }
        done += n;
        if (progress) progress(done, total);
    }
    String md5got; nfGetEnd(md5got);
    if (done != total) { Update.abort(); err = "primit " + String(done) + " din " + String(total) + " bytes"; return false; }
    if (info.md5.length() == 32 && !md5got.equalsIgnoreCase(info.md5)) { Update.abort(); err = "MD5 diferit: server " + info.md5 + ", primit " + md5got; return false; }
    if (!Update.end(true)) { err = "Update.end: " + String(Update.errorString()); return false; }
    return true;
}

String otaZone() {
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    String s = "zona curenta " + String(run ? run->label : "?");
    s += ", urmatoarea " + String(next ? next->label : "(fara - schema de partitii fara OTA)");
    return s;
}

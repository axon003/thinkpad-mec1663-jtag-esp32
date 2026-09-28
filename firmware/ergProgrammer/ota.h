/**
 * ota.h — update firmware prin WiFi (OTA CHECK / OTA UPDATE), ca la ergCAN
 *   - otaCheck:  GET <ota.url>?fw=<ver>&dev=<mac> -> {"fw":"2.1","url":"...ergprogrammer_dl.php","md5":"...","size":N,"notes":"..."}
 *   - otaUpdate: descarca .bin-ul in flux (Content-Length obligatoriu), verifica MD5, scrie in zona OTA libera
 *   Fara mecanismul de revenire automata din ergCAN: placa sta pe banc, pe USB - un firmware prost
 *   se reflasheaza cu tools/flash.cmd. Declansare DOAR manuala din shell.
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0)
 */
#ifndef MEMPROG_OTA_H
#define MEMPROG_OTA_H

#include <Arduino.h>

struct OtaInfo {
    String latest, url, md5, notes;
    uint32_t size;
    bool newer;
};

typedef void (*OtaProgress)(size_t done, size_t total);

bool otaCheck(OtaInfo &info, String &err);
bool otaUpdate(const OtaInfo &info, String &err, OtaProgress progress);   // true = scris, cere REBOOT
int  otaVersionCmp(const char *a, const char *b);                          // <0, 0, >0
String otaZone();                                                          // zona curenta / urmatoarea

#endif // MEMPROG_OTA_H

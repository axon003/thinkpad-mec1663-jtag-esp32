/**
 * netfile.h — fisiere pe retea: READ trimite dump-ul la <url.base>, WRITE/VERIFY il iau de acolo
 *   HTTP scris de mana peste WiFiClient (http) / WiFiClientSecure (https, ISRG Root X1), ca sa pot
 *   STREAMA: upload cu Content-Length cunoscut dinainte (nu tin 256 KB in RAM), download cu
 *   Range (paginile de refacut la CLEAR). MD5 calculat in ambele sensuri, afisat la final ca sa
 *   compar cu md5sum-ul de pe server. FARA erori silent: orice esec intoarce false + motiv.
 *
 *   Upload:   url.put gol  -> PUT  <base>/<nume>
 *             url.put=x    -> POST <base>/x?f=<nume>[&append=1]   (server/put.php, host/serve_files.py)
 *   Download: GET <base>/<nume> [Range: bytes=a-b]
 *   Header X-Token: <url.token> daca e setat.
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0)
 */
#ifndef MEMPROG_NETFILE_H
#define MEMPROG_NETFILE_H

#include <Arduino.h>

// URL absolut pentru un nume de fisier relativ la url.base ("" daca base lipseste).
String nfUrlFor(const String &name);
bool   nfConfigured();

// --- upload in flux ---
bool nfPutBegin(const String &name, uint32_t total, bool append, String &err);
bool nfPutWrite(const uint8_t *data, size_t n, String &err);
bool nfPutEnd(String &md5hex, String &err);          // asteapta raspunsul serverului (2xx)
void nfPutAbort();

// --- download in flux ---
// len = 0 -> tot fisierul. contentLen = cat va veni (din Content-Length / Content-Range).
bool nfGetBegin(const String &name, uint32_t off, uint32_t len, uint32_t &contentLen, String &err);
int  nfGetRead(uint8_t *buf, size_t n, String &err);  // >0 bytes, 0 = sfarsit, -1 = eroare (err setat)
void nfGetEnd(String &md5hex);

// GET mic (JSON versiune OTA, listare) - body in String, max maxLen.
bool nfGetSmall(const String &url, String &body, String &err, size_t maxLen = 4096);

// POST mic cu body gata (log consola, notificari). Intoarce true pe 2xx.
bool nfPostSmall(const String &url, const String &body, const char *contentType, String &resp, String &err);

#endif // MEMPROG_NETFILE_H

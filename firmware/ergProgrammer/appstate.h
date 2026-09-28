/*
 * appstate.h - stare partajata (log live + status operatie), task-safe
 * Versiune: 1.0
 *
 * Un singur loc pentru log-ul live si statusul curent, accesat din:
 *  - task-ul de operatie (scrie log/progress/result)
 *  - loop() / ui_display (citeste si deseneaza pe TFT)
 *  - web_ui (citeste pentru /api/status si /api/log)
 * Protejat cu un mutex FreeRTOS (log-ul foloseste String => heap).
 *
 * Fanout FARA erori silent: applogErr(...) marcheaza linia ca EROARE (rosu pe
 * TFT/web) si o scrie si pe Serial ca "# ERR ...".
 */

#ifndef MEMPROG_APPSTATE_H
#define MEMPROG_APPSTATE_H

#include <Arduino.h>

#define LOG_INFO 0
#define LOG_OK   1
#define LOG_ERR  2
#define LOG_PROG 3

#define APPLOG_CAP 24   // linii pastrate in ring pentru TFT2 + web

void appstateBegin();

// --- log ---
void applogAdd(uint8_t kind, const String &msg);
static inline void applogInfo(const String &m) { applogAdd(LOG_INFO, m); }
static inline void applogOk(const String &m)   { applogAdd(LOG_OK, m); }
static inline void applogErr(const String &m)  { applogAdd(LOG_ERR, m); }

// --- status ---
void stSetWifi(const String &apSsid, const String &staIp);
void stSetDriver(const String &drv);
void stSetOp(const String &op);
void stSetProgress(int pct);          // -1 = fara bara
void stSetResult(bool ok, const String &msg);
void stSetDevice(const String &info); // IDCODE/ARC6xx sau JEDEC ID
void stClearResult();

// Marcaj "s-a schimbat ceva" pentru redesenarea TFT (loop verifica si reseteaza).
bool stDirtyTakeStatus();  // true daca statusul s-a schimbat de la ultima verificare
uint32_t stLogSeq();       // secventa curenta a log-ului (pentru redraw TFT2)

// --- copii sub lock, pentru desenare / JSON ---
struct StatusCopy {
  String apSsid, staIp, driver, op, resultMsg, device;
  int progress;
  int8_t resultOk; // -1 necunoscut, 0 err, 1 ok
};
void stGetStatus(StatusCopy &out);

// JSON pentru web (escapat). getLogJson intoarce doar liniile cu seq > since.
void stGetStatusJson(String &out);
void stGetLogJson(uint32_t since, String &out);

// Copiaza ultimele maxN linii (cronologic) pentru desenare pe TFT2.
void stGetLogLines(int maxN, String *outText, uint8_t *outKind, int &n);

#endif // MEMPROG_APPSTATE_H

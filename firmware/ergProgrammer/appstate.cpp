/*
 * appstate.cpp - implementare stare partajata (log + status)
 * Versiune: 1.0
 */

#include "appstate.h"
#include "config.h"   // FW_NAME / FW_VERSION
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t g_mux = nullptr;

struct LogLine { uint32_t seq; uint8_t kind; String text; };
static LogLine g_log[APPLOG_CAP];
static uint16_t g_head = 0;      // index urmator de scris
static uint16_t g_count = 0;
static uint32_t g_seq = 0;

static StatusCopy g_st;
static volatile bool g_dirty = true;

static inline void lock()   { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
static inline void unlock() { if (g_mux) xSemaphoreGive(g_mux); }

void appstateBegin() {
  g_mux = xSemaphoreCreateMutex();
  g_st.progress = -1;
  g_st.resultOk = -1;
  g_st.driver = "-";
  g_st.op = "idle";
}

static String jsonEsc(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': break;
      case '\t': o += " ";    break;
      default:
        if ((uint8_t)c < 0x20) o += ' ';
        else o += c;
    }
  }
  return o;
}

void applogAdd(uint8_t kind, const String &msg) {
  lock();
  g_seq++;
  g_log[g_head].seq = g_seq;
  g_log[g_head].kind = kind;
  g_log[g_head].text = msg;
  g_head = (g_head + 1) % APPLOG_CAP;
  if (g_count < APPLOG_CAP) g_count++;
  g_dirty = true;
  unlock();
  // fanout pe Serial (prefix '#' ca sa nu strice protocolul host-ului)
  const char *tag = kind == LOG_ERR ? "# ERR " : (kind == LOG_OK ? "# OK " : "# ");
  Serial.print(tag); Serial.println(msg);
}

void stSetWifi(const String &apSsid, const String &staIp) {
  lock(); g_st.apSsid = apSsid; g_st.staIp = staIp; g_dirty = true; unlock();
}
void stSetDriver(const String &drv) { lock(); g_st.driver = drv; g_dirty = true; unlock(); }
void stSetOp(const String &op)      { lock(); g_st.op = op; g_dirty = true; unlock(); }
void stSetProgress(int pct)         { lock(); g_st.progress = pct; g_dirty = true; unlock(); }
void stSetDevice(const String &i)   { lock(); g_st.device = i; g_dirty = true; unlock(); }
void stClearResult()                { lock(); g_st.resultOk = -1; g_st.resultMsg = ""; g_dirty = true; unlock(); }
void stSetResult(bool ok, const String &msg) {
  lock(); g_st.resultOk = ok ? 1 : 0; g_st.resultMsg = msg; g_dirty = true; unlock();
}

bool stDirtyTakeStatus() { bool d; lock(); d = g_dirty; g_dirty = false; unlock(); return d; }
uint32_t stLogSeq()      { uint32_t s; lock(); s = g_seq; unlock(); return s; }

void stGetStatus(StatusCopy &out) { lock(); out = g_st; unlock(); }

void stGetStatusJson(String &out) {
  lock();
  out = "{";
  out += "\"fw\":\"" FW_NAME " " FW_VERSION "\",";
  out += "\"ap\":\"" + jsonEsc(g_st.apSsid) + "\",";
  out += "\"staip\":\"" + jsonEsc(g_st.staIp) + "\",";
  out += "\"driver\":\"" + jsonEsc(g_st.driver) + "\",";
  out += "\"op\":\"" + jsonEsc(g_st.op) + "\",";
  out += "\"progress\":" + String(g_st.progress) + ",";
  out += "\"resultOk\":" + String((int)g_st.resultOk) + ",";
  out += "\"resultMsg\":\"" + jsonEsc(g_st.resultMsg) + "\",";
  out += "\"device\":\"" + jsonEsc(g_st.device) + "\",";
  out += "\"logSeq\":" + String(g_seq);
  out += "}";
  unlock();
}

void stGetLogJson(uint32_t since, String &out) {
  lock();
  out = "{\"lines\":[";
  bool first = true;
  // parcurgem in ordinea cronologica: cel mai vechi are seq = g_seq-g_count+1
  uint32_t oldest = (g_seq >= g_count) ? (g_seq - g_count + 1) : 1;
  for (uint32_t s = oldest; s <= g_seq; s++) {
    if (s <= since) continue;
    // gaseste linia cu acest seq
    for (uint16_t i = 0; i < g_count; i++) {
      uint16_t idx = (g_head + APPLOG_CAP - g_count + i) % APPLOG_CAP;
      if (g_log[idx].seq == s) {
        if (!first) out += ",";
        first = false;
        out += "{\"seq\":" + String(s) + ",\"k\":" + String(g_log[idx].kind) +
               ",\"t\":\"" + jsonEsc(g_log[idx].text) + "\"}";
        break;
      }
    }
  }
  out += "],\"seq\":" + String(g_seq) + "}";
  unlock();
}

void stGetLogLines(int maxN, String *outText, uint8_t *outKind, int &n) {
  lock();
  int avail = (int)g_count;
  int take = avail < maxN ? avail : maxN;
  n = take;
  // ultimele 'take' linii, cronologic (cea mai veche prima)
  for (int i = 0; i < take; i++) {
    int idx = (g_head + APPLOG_CAP - take + i) % APPLOG_CAP;
    outText[i] = g_log[idx].text;
    outKind[i] = g_log[idx].kind;
  }
  unlock();
}

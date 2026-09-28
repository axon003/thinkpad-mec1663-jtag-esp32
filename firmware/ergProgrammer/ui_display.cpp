/*
 * ui_display.cpp - implementare feedback 2x TFT ILI9341
 * Versiune: 1.0
 */

#include "ui_display.h"
#include "config.h"
#include "appstate.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

static SPIClass hspi(HSPI);
static Adafruit_ILI9341 tft1(&hspi, PIN_TFT_DC, PIN_TFT_CS1, PIN_TFT_RST);
static Adafruit_ILI9341 tft2(&hspi, PIN_TFT_DC, PIN_TFT_CS2, PIN_TFT_RST);
static bool g_present[2] = { false, false };
static uint16_t g_id[2] = { 0, 0 };

static uint32_t g_lastLogSeq = 0xFFFFFFFF;

static uint16_t kindColor(uint8_t k) {
  switch (k) {
    case LOG_ERR:  return ILI9341_RED;
    case LOG_OK:   return ILI9341_GREEN;
    case LOG_PROG: return ILI9341_CYAN;
    default:       return ILI9341_WHITE;
  }
}

void uiBegin() {
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH); // backlight on
  hspi.begin(PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_MOSI, -1);

  tft1.begin();
  tft2.begin();
  tft1.setRotation(1); // 320x240 landscape
  tft2.setRotation(1);

  // best-effort detectie (necesita MISO; daca nu raspunde, continuam oricum)
  g_id[0] = tft1.readcommand8(ILI9341_RDSELFDIAG); // orice raspuns nenul = viu
  g_id[1] = tft2.readcommand8(ILI9341_RDSELFDIAG);
  g_present[0] = true; // desenam oricum; flag informativ
  g_present[1] = true;

  tft1.fillScreen(ILI9341_BLACK);
  tft2.fillScreen(ILI9341_BLACK);

  applogInfo(String(FW_NAME) + " " + FW_VERSION + " pornit");
}

bool uiPresent(int idx)    { return (idx >= 0 && idx < 2) ? g_present[idx] : false; }
uint16_t uiReadId(int idx) { return (idx >= 0 && idx < 2) ? g_id[idx] : 0; }

static void drawStatus() {
  StatusCopy s;
  stGetStatus(s);

  tft1.fillScreen(ILI9341_BLACK);

  // bara titlu
  tft1.fillRect(0, 0, 320, 22, ILI9341_NAVY);
  tft1.setTextColor(ILI9341_WHITE);
  tft1.setTextSize(2);
  tft1.setCursor(4, 3);
  tft1.print(FW_NAME);

  tft1.setTextSize(1);
  int y = 30;
  tft1.setTextColor(ILI9341_CYAN);   tft1.setCursor(4, y); tft1.print("AP:  "); tft1.setTextColor(ILI9341_WHITE); tft1.print(s.apSsid); y += 14;
  tft1.setTextColor(ILI9341_CYAN);   tft1.setCursor(4, y); tft1.print("STA: "); tft1.setTextColor(ILI9341_WHITE); tft1.print(s.staIp.length() ? s.staIp : "-"); y += 20;

  tft1.setTextSize(2);
  tft1.setTextColor(ILI9341_YELLOW); tft1.setCursor(4, y); tft1.print("DRV: "); tft1.setTextColor(ILI9341_WHITE); tft1.print(s.driver); y += 22;
  tft1.setTextColor(ILI9341_YELLOW); tft1.setCursor(4, y); tft1.print("OP:  "); tft1.setTextColor(ILI9341_WHITE); tft1.print(s.op); y += 24;

  // progres
  if (s.progress >= 0) {
    int pct = s.progress > 100 ? 100 : s.progress;
    tft1.drawRect(4, y, 312, 18, ILI9341_WHITE);
    tft1.fillRect(6, y + 2, (308 * pct) / 100, 14, ILI9341_GREEN);
    tft1.setTextSize(1);
    tft1.setTextColor(ILI9341_WHITE);
    tft1.setCursor(140, y + 5);
    tft1.print(String(pct) + "%");
    y += 24;
  } else {
    y += 6;
  }

  // device detectat
  tft1.setTextSize(1);
  tft1.setTextColor(ILI9341_MAGENTA); tft1.setCursor(4, y); tft1.print("DEV: ");
  tft1.setTextColor(ILI9341_WHITE);   tft1.print(s.device.length() ? s.device : "-"); y += 20;

  // rezultat
  if (s.resultOk == 1) {
    tft1.fillRect(0, 214, 320, 26, ILI9341_DARKGREEN);
    tft1.setTextColor(ILI9341_WHITE); tft1.setTextSize(1);
    tft1.setCursor(4, 222); tft1.print("OK: " + s.resultMsg);
  } else if (s.resultOk == 0) {
    tft1.fillRect(0, 214, 320, 26, ILI9341_RED);
    tft1.setTextColor(ILI9341_WHITE); tft1.setTextSize(1);
    tft1.setCursor(4, 222); tft1.print("EROARE: " + s.resultMsg);
  }
}

static void drawLog() {
  static const int MAXL = 22;
  String txt[MAXL];
  uint8_t knd[MAXL];
  int n = 0;
  stGetLogLines(MAXL, txt, knd, n);

  tft2.fillScreen(ILI9341_BLACK);
  tft2.fillRect(0, 0, 320, 14, ILI9341_DARKGREY);
  tft2.setTextColor(ILI9341_WHITE); tft2.setTextSize(1);
  tft2.setCursor(4, 3); tft2.print("LOG");

  tft2.setTextSize(1);
  int y = 18;
  for (int i = 0; i < n; i++) {
    tft2.setTextColor(kindColor(knd[i]));
    tft2.setCursor(2, y);
    // trunchiere la ~52 caractere (320/6)
    String t = txt[i];
    if (t.length() > 52) t = t.substring(0, 52);
    tft2.print(t);
    y += 10;
    if (y > 232) break;
  }
}

void uiTick() {
  if (stDirtyTakeStatus()) drawStatus();
  uint32_t seq = stLogSeq();
  if (seq != g_lastLogSeq) { g_lastLogSeq = seq; drawLog(); }
}

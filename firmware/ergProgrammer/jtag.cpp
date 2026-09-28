/*
 * jtag.cpp - implementare JTAG TAP bit-bang
 * Versiune: 2.1 (stare TAP, fara treceri RTI parazite)
 */

#include "jtag.h"
#include "config.h"
#include "pins.h"
#include "driver/gpio.h"

inline void Jtag::halfDelay() {
  // v2.0: viteza din setari (SET jtag.us); 0 = doar un NOP
  if (g_pin.jtagUs) delayMicroseconds(g_pin.jtagUs);
  else __asm__ __volatile__("nop");
}

void Jtag::begin() {
  pinMode(g_pin.tck, OUTPUT);
  pinMode(g_pin.tms, OUTPUT);
  pinMode(g_pin.tdi, OUTPUT);
  pinMode(g_pin.tdo, INPUT);
  if (g_pin.trst >= 0) {
    pinMode(g_pin.trst, OUTPUT);
    digitalWrite(g_pin.trst, HIGH); // TRST inactiv (activ pe low)
  }
  // fronturi mai lente = mai putin ringing pe firele lungi (dublu-ceas la MEC)
  gpio_drive_cap_t cap = (gpio_drive_cap_t)g_pin.jtagDrv;
  gpio_set_drive_capability((gpio_num_t)g_pin.tck, cap);
  gpio_set_drive_capability((gpio_num_t)g_pin.tms, cap);
  gpio_set_drive_capability((gpio_num_t)g_pin.tdi, cap);
  digitalWrite(g_pin.tck, LOW);
  digitalWrite(g_pin.tms, HIGH);
  digitalWrite(g_pin.tdi, LOW);
}

// Esantionam TDO cat timp TCK e LOW (stabil dupa frontul descendent anterior),
// apoi urcam TCK (tinta capteaza TMS/TDI pe frontul ascendent) si coboram.
uint8_t Jtag::pulse(uint8_t tms, uint8_t tdi) {
  digitalWrite(g_pin.tms, tms ? HIGH : LOW);
  digitalWrite(g_pin.tdi, tdi ? HIGH : LOW);
  halfDelay();
  uint8_t tdo = (uint8_t)digitalRead(g_pin.tdo);
  digitalWrite(g_pin.tck, HIGH);
  halfDelay();
  digitalWrite(g_pin.tck, LOW);
  return tdo;
}

// v2.1: stare TAP urmarita. ARC porneste o tranzactie la FIECARE ceas in Run-Test/Idle (quirk
// debug_arc/Glasgow), deci dupa shift ramanem in Update-xR si NU trecem prin RTI decat in runTestIdle().
// Varianta 1.0/2.0 trecea prin RTI dupa fiecare IR/DR => tranzactii fantoma => DR_STATUS.FL=1.
enum { TAP_TLR, TAP_RTI, TAP_UPD };
static uint8_t s_tap = TAP_TLR;

void Jtag::testReset() {
  // 5 pulsuri cu TMS=1 => Test-Logic-Reset (garantat din orice stare). Ramanem in TLR.
  for (int i = 0; i < 5; i++) pulse(1, 0);
  s_tap = TAP_TLR;
}

void Jtag::runTestIdle(uint32_t clocks) {
  if (s_tap != TAP_RTI) { pulse(0, 0); s_tap = TAP_RTI; }   // TLR/Update-xR -> RTI
  for (uint32_t i = 0; i < clocks; i++) pulse(0, 0);
}


void Jtag::writeIR(uint32_t value, uint8_t nbits) {
  if (s_tap == TAP_TLR) { pulse(0, 0); s_tap = TAP_RTI; }
  // RTI|Update -> Select-DR -> Select-IR -> Capture-IR -> Shift-IR : TMS = 1,1,0,0
  pulse(1, 0);
  pulse(1, 0);
  pulse(0, 0);
  pulse(0, 0);
  // Shift nbits, LSB-first; ultimul bit cu TMS=1 (Shift-IR -> Exit1-IR).
  for (uint8_t i = 0; i < nbits; i++) {
    uint8_t bit = (value >> i) & 1;
    uint8_t last = (i == (uint8_t)(nbits - 1));
    pulse(last ? 1 : 0, bit);
  }
  // Exit1-IR -> Update-IR (ramanem aici)
  pulse(1, 0);
  s_tap = TAP_UPD;
}

void Jtag::writeDR(uint32_t value, uint8_t nbits) {
  exchangeDR(value, nbits);
}

uint32_t Jtag::readDR(uint8_t nbits) {
  return exchangeDR(0, nbits);
}

uint32_t Jtag::exchangeDR(uint32_t value, uint8_t nbits) {
  uint32_t out = 0;
  if (s_tap == TAP_TLR) { pulse(0, 0); s_tap = TAP_RTI; }
  // RTI|Update -> Select-DR -> Capture-DR -> Shift-DR : TMS = 1,0,0
  pulse(1, 0);
  pulse(0, 0);
  pulse(0, 0);
  for (uint8_t i = 0; i < nbits; i++) {
    uint8_t bit = (value >> i) & 1;
    uint8_t last = (i == (uint8_t)(nbits - 1));
    uint8_t tdo = pulse(last ? 1 : 0, bit);
    if (tdo) out |= ((uint32_t)1 << i);
  }
  // Exit1-DR -> Update-DR (ramanem aici)
  pulse(1, 0);
  s_tap = TAP_UPD;
  return out;
}

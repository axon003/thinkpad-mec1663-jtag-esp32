/*
 * microwire93c.cpp - implementare driver Microwire 93C (optional)
 * Versiune: 1.0
 */

#include "microwire93c.h"
#include "config.h"
#include "pins.h"

#define MW_HALF_US 2

void Microwire93c::begin(uint8_t addrBits) {
  _addrBits = addrBits;
  pinMode(g_pin.mwcs, OUTPUT);
  pinMode(g_pin.mwsk, OUTPUT);
  pinMode(g_pin.mwdi, OUTPUT);
  pinMode(g_pin.mwdo, INPUT);
  digitalWrite(g_pin.mwcs, LOW);
  digitalWrite(g_pin.mwsk, LOW);
  digitalWrite(g_pin.mwdi, LOW);
}

void Microwire93c::clk() {
  digitalWrite(g_pin.mwsk, HIGH);
  delayMicroseconds(MW_HALF_US);
  digitalWrite(g_pin.mwsk, LOW);
  delayMicroseconds(MW_HALF_US);
}

void Microwire93c::sendBit(uint8_t b) {
  digitalWrite(g_pin.mwdi, b ? HIGH : LOW);
  clk();
}

void Microwire93c::sendBits(uint16_t val, uint8_t nbits) {
  for (int i = nbits - 1; i >= 0; i--) sendBit((val >> i) & 1);
}

void Microwire93c::csEnable()  { digitalWrite(g_pin.mwcs, HIGH); delayMicroseconds(MW_HALF_US); }
void Microwire93c::csDisable() { digitalWrite(g_pin.mwcs, LOW);  delayMicroseconds(MW_HALF_US); }

void Microwire93c::startBit() {
  // Start bit = 1 (dupa ce CS a fost ridicat).
  sendBit(1);
}

uint16_t Microwire93c::readWord(uint16_t wordAddr) {
  csEnable();
  startBit();
  sendBits(0b10, 2);                 // opcode READ
  sendBits(wordAddr, _addrBits);
  // Un bit dummy (0) apoi 16 biti de date, MSB first, pe DO.
  uint16_t val = 0;
  for (int i = 0; i < 16; i++) {
    clk();
    val = (val << 1) | (digitalRead(g_pin.mwdo) ? 1 : 0);
  }
  csDisable();
  return val;
}

void Microwire93c::ewen() {
  csEnable();
  startBit();
  sendBits(0b00, 2);                 // opcode 00 = mod extins
  sendBits(0b11, 2);                 // 11xxxx = EWEN
  for (int i = 0; i < _addrBits - 2; i++) sendBit(0);
  csDisable();
}

void Microwire93c::ewds() {
  csEnable();
  startBit();
  sendBits(0b00, 2);
  sendBits(0b00, 2);                 // 00xxxx = EWDS
  for (int i = 0; i < _addrBits - 2; i++) sendBit(0);
  csDisable();
}

bool Microwire93c::waitReady(uint32_t timeoutMs) {
  // Dupa WRITE, ridicand CS, DO trece LOW (busy) apoi HIGH (ready).
  csEnable();
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (digitalRead(g_pin.mwdo)) { csDisable(); return true; }
    delay(1);
  }
  csDisable();
  return false;
}

bool Microwire93c::writeWord(uint16_t wordAddr, uint16_t value) {
  csEnable();
  startBit();
  sendBits(0b01, 2);                 // opcode WRITE
  sendBits(wordAddr, _addrBits);
  sendBits(value, 16);
  csDisable();
  return waitReady(50);
}

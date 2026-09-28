/*
 * microwire93c.h - driver Microwire 93C46/56/66 (optional)
 * Versiune: 1.0
 *
 * STATUS: driver standard, bit-bang. Organizare x16 (ORG=1) implicita.
 * 93C46=64 words, 93C56=128, 93C66=256 (x16). Adresa in words.
 * Instructiuni: READ(10), WRITE(01), EWEN(00 11xx), EWDS(00 00xx), ERAL, WRAL.
 */

#ifndef MEMPROG_MICROWIRE93C_H
#define MEMPROG_MICROWIRE93C_H

#include <Arduino.h>

class Microwire93c {
public:
  // addrBits: 6 pentru 93C46, 7 pentru 93C56, 8 pentru 93C66 (organizare x16).
  void begin(uint8_t addrBits = 6);

  uint16_t readWord(uint16_t wordAddr);
  bool     writeWord(uint16_t wordAddr, uint16_t value);

  void ewen();  // enable erase/write
  void ewds();  // disable erase/write

private:
  uint8_t _addrBits;

  void clk();
  void sendBit(uint8_t b);
  void sendBits(uint16_t val, uint8_t nbits);
  void startBit();
  void csEnable();
  void csDisable();
  bool waitReady(uint32_t timeoutMs);
};

#endif // MEMPROG_MICROWIRE93C_H

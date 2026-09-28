/*
 * jtag.h - JTAG TAP bit-bang pe GPIO ESP32
 * Versiune: 1.0
 *
 * Implementeaza masina de stari TAP (Test-Logic-Reset, Run-Test/Idle,
 * Shift-IR, Shift-DR) si scan-uri IR/DR. Conventie JTAG standard: bitii se
 * deplaseaza LSB-first. TDO este esantionat cat timp TCK e LOW (valoarea a
 * fost actualizata pe frontul descendent anterior), iar tinta captureaza
 * TDI/TMS pe frontul ascendent.
 *
 * QUIRK ARC (vezi debug_arc): TOATE tranzactiile ARC se initiaza printr-un
 * puls TCK in starea Run-Test/Idle. De aceea expunem runTestIdle().
 */

#ifndef MEMPROG_JTAG_H
#define MEMPROG_JTAG_H

#include <Arduino.h>

class Jtag {
public:
  void begin();

  // Test-Logic-Reset: 5 pulsuri cu TMS=1, apoi ramanem in Run-Test/Idle.
  void testReset();

  // Ramai in Run-Test/Idle si da 'clocks' pulsuri (TMS=0).
  void runTestIdle(uint32_t clocks);

  // Scrie registrul de instructiune (IR). 'nbits' biti, LSB-first.
  void writeIR(uint32_t value, uint8_t nbits);

  // Scrie registrul de date (DR). LSB-first.
  void writeDR(uint32_t value, uint8_t nbits);

  // Citeste DR: deplaseaza 'nbits' zerouri pe TDI si intoarce TDO asamblat
  // LSB-first. (Pentru IDCODE si pentru citirea rezultatului tranzactiei.)
  uint32_t readDR(uint8_t nbits);

  // Scrie si citeste simultan DR (shift value in, citeste vechea valoare).
  uint32_t exchangeDR(uint32_t value, uint8_t nbits);

private:
  // Un puls de ceas: aplica tms/tdi, esantioneaza si intoarce TDO.
  uint8_t pulse(uint8_t tms, uint8_t tdi);
  inline void halfDelay();
};

#endif // MEMPROG_JTAG_H

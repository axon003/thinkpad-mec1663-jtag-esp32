/*
 * i2c_eeprom.h - driver I2C EEPROM 24-series (24C02 .. 24C512)
 * Versiune: 1.0
 *
 * STATUS: driver standard, de incredere. Suporta adresare interna pe 1 sau 2
 * bytes si scriere paginata. Pentru 24C02..24C16 adresa e pe 1 byte iar bitii
 * de bloc intra in adresa I2C; pentru 24C32..24C512 adresa interna e pe 2 bytes.
 */

#ifndef MEMPROG_I2C_EEPROM_H
#define MEMPROG_I2C_EEPROM_H

#include <Arduino.h>

class I2cEeprom {
public:
  // i2cAddr7 = adresa de baza 7-bit (tipic 0x50).
  // addrBytes = 1 sau 2 (marimea adresei interne).
  // pageSize  = dimensiunea paginii de scriere (8/16/32/64/128 dupa cip).
  void begin(uint8_t i2cAddr7 = 0x50, uint8_t addrBytes = 2, uint16_t pageSize = 32);

  bool read(uint32_t addr, uint8_t *buf, uint32_t len, String &err);
  bool write(uint32_t addr, const uint8_t *buf, uint32_t len, String &err);

  // Probe rapid: incearca un start pe adresa; true daca cipul da ACK.
  bool probe();

private:
  uint8_t  _base;
  uint8_t  _addrBytes;
  uint16_t _pageSize;

  // Pentru 24C02..24C16 (addrBytes=1) blocul intra in adresa I2C (bitii A0..A2).
  uint8_t devAddrFor(uint32_t addr);
  void    sendMemAddr(uint32_t addr);
  bool    waitReady(uint8_t dev, uint32_t timeoutMs, String &err); // ACK polling
  bool    writePage(uint32_t addr, const uint8_t *buf, uint16_t len, String &err);
};

#endif // MEMPROG_I2C_EEPROM_H

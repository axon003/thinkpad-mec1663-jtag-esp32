/*
 * i2c_eeprom.cpp - implementare driver I2C EEPROM 24-series
 * Versiune: 1.0
 */

#include "i2c_eeprom.h"
#include "config.h"
#include "pins.h"
#include <Wire.h>

void I2cEeprom::begin(uint8_t i2cAddr7, uint8_t addrBytes, uint16_t pageSize) {
  _base = i2cAddr7;
  _addrBytes = addrBytes;
  _pageSize = pageSize;
  Wire.end();
  Wire.begin(g_pin.sda, g_pin.scl, g_pin.i2cHz);   // v2.0: pini din setari
}

uint8_t I2cEeprom::devAddrFor(uint32_t addr) {
  if (_addrBytes == 1) {
    // 24C02..24C16: bitii de bloc peste 8 biti intra in A0..A2 ale adresei I2C.
    uint8_t block = (uint8_t)((addr >> 8) & 0x07);
    return _base | block;
  }
  return _base;
}

void I2cEeprom::sendMemAddr(uint32_t addr) {
  if (_addrBytes == 2) Wire.write((uint8_t)((addr >> 8) & 0xFF));
  Wire.write((uint8_t)(addr & 0xFF));
}

bool I2cEeprom::probe() {
  Wire.beginTransmission(_base);
  return Wire.endTransmission() == 0;
}

// ACK polling: dupa un write, cipul nu da ACK pana termina ciclul intern.
bool I2cEeprom::waitReady(uint8_t dev, uint32_t timeoutMs, String &err) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    Wire.beginTransmission(dev);
    if (Wire.endTransmission() == 0) return true;
    delay(1);
  }
  err = "I2C EEPROM: timeout ACK polling (dev 0x" + String(dev, HEX) + ")";
  return false;
}

bool I2cEeprom::read(uint32_t addr, uint8_t *buf, uint32_t len, String &err) {
  uint32_t done = 0;
  while (done < len) {
    uint8_t dev = devAddrFor(addr + done);
    // Wire are buffer limitat (tipic 32B pe multe platforme; ESP32 ~128).
    uint32_t chunk = len - done;
    if (chunk > 128) chunk = 128;
    Wire.beginTransmission(dev);
    sendMemAddr(addr + done);
    if (Wire.endTransmission(false) != 0) { // repeated start
      err = "I2C EEPROM: NAK la setarea adresei de citire";
      return false;
    }
    uint32_t got = Wire.requestFrom((int)dev, (int)chunk);
    if (got != chunk) {
      err = "I2C EEPROM: citire scurta (" + String(got) + "/" + String(chunk) + ")";
      return false;
    }
    for (uint32_t i = 0; i < chunk; i++) buf[done + i] = Wire.read();
    done += chunk;
  }
  return true;
}

bool I2cEeprom::writePage(uint32_t addr, const uint8_t *buf, uint16_t len, String &err) {
  uint8_t dev = devAddrFor(addr);
  Wire.beginTransmission(dev);
  sendMemAddr(addr);
  for (uint16_t i = 0; i < len; i++) Wire.write(buf[i]);
  if (Wire.endTransmission() != 0) {
    err = "I2C EEPROM: NAK la scriere pagina @0x" + String(addr, HEX);
    return false;
  }
  return waitReady(dev, 100, err);
}

bool I2cEeprom::write(uint32_t addr, const uint8_t *buf, uint32_t len, String &err) {
  uint32_t done = 0;
  while (done < len) {
    uint32_t pageOff = (addr + done) % _pageSize;
    uint32_t chunk = _pageSize - pageOff;
    if (chunk > (len - done)) chunk = len - done;
    if (chunk > 128) chunk = 128; // limita buffer Wire
    if (!writePage(addr + done, buf + done, (uint16_t)chunk, err)) return false;
    done += chunk;
  }
  return true;
}

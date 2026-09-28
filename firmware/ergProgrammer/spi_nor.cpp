/*
 * spi_nor.cpp - implementare driver SPI NOR 25-series
 * Versiune: 1.0
 */

#include "spi_nor.h"
#include "config.h"
#include "pins.h"
#include <SPI.h>

#define g_spiSettings SPISettings(g_pin.spiHz, MSBFIRST, SPI_MODE0)   // v2.0: frecventa din setari

void SpiNor::begin() {
  if (_begun) return;               // idempotent - lazy init
  _begun = true;
  pinMode(g_pin.cs, OUTPUT);
  digitalWrite(g_pin.cs, HIGH);
  // VSPI: SCK, MISO, MOSI, SS
  SPI.begin(g_pin.sck, g_pin.miso, g_pin.mosi, g_pin.cs);
}

void SpiNor::end() {
  if (!_begun) return;
  SPI.end();
  _begun = false;
  _fourByte = false;
}

void SpiNor::csLow()  { digitalWrite(g_pin.cs, LOW); }
void SpiNor::csHigh() { digitalWrite(g_pin.cs, HIGH); }

void SpiNor::set4ByteMode(bool enable) {
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(enable ? SNOR_CMD_EN4B : SNOR_CMD_EX4B);
  csHigh();
  SPI.endTransaction();
  _fourByte = enable;
}

void SpiNor::sendAddr(uint32_t addr) {
  if (_fourByte) SPI.transfer((addr >> 24) & 0xFF);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
}

uint8_t SpiNor::readStatus() {
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(SNOR_CMD_RDSR);
  uint8_t sr = SPI.transfer(0x00);
  csHigh();
  SPI.endTransaction();
  return sr;
}

void SpiNor::writeEnable() {
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(SNOR_CMD_WREN);
  csHigh();
  SPI.endTransaction();
}

bool SpiNor::waitWip(uint32_t timeoutMs, String &err) {
  uint32_t t0 = millis();
  while (readStatus() & SNOR_SR_WIP) {
    if (millis() - t0 > timeoutMs) {
      err = "SPI NOR: timeout WIP (write in progress)";
      return false;
    }
    delay(1);
  }
  return true;
}

SpiNorId SpiNor::readId() {
  SpiNorId id;
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(SNOR_CMD_RDID);
  id.manufacturer = SPI.transfer(0x00);
  id.memType      = SPI.transfer(0x00);
  id.capacity     = SPI.transfer(0x00);
  csHigh();
  SPI.endTransaction();
  // Multe cip-uri codeaza dimensiunea ca 2^capacity bytes.
  if (id.capacity >= 0x10 && id.capacity <= 0x25)
    id.sizeBytes = (uint32_t)1 << id.capacity;
  else
    id.sizeBytes = 0;
  return id;
}

void SpiNor::read(uint32_t addr, uint8_t *buf, uint32_t len) {
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(_fourByte ? SNOR_CMD_READ4B : SNOR_CMD_READ);
  sendAddr(addr);
  for (uint32_t i = 0; i < len; i++) buf[i] = SPI.transfer(0x00);
  csHigh();
  SPI.endTransaction();
}

bool SpiNor::pageProgram(uint32_t addr, const uint8_t *buf, uint32_t len, String &err) {
  writeEnable();
  if (!(readStatus() & SNOR_SR_WEL)) {
    err = "SPI NOR: WREN esuat (WEL nu s-a setat) - cip protejat la scriere?";
    return false;
  }
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(_fourByte ? SNOR_CMD_PP4B : SNOR_CMD_PP);
  sendAddr(addr);
  for (uint32_t i = 0; i < len; i++) SPI.transfer(buf[i]);
  csHigh();
  SPI.endTransaction();
  return waitWip(5000, err);
}

bool SpiNor::program(uint32_t addr, const uint8_t *buf, uint32_t len, String &err) {
  uint32_t done = 0;
  while (done < len) {
    uint32_t pageOff = (addr + done) % SNOR_PAGE_SIZE;
    uint32_t chunk = SNOR_PAGE_SIZE - pageOff;
    if (chunk > (len - done)) chunk = len - done;
    if (!pageProgram(addr + done, buf + done, chunk, err)) return false;
    done += chunk;
  }
  return true;
}

bool SpiNor::sectorErase(uint32_t addr, String &err) {
  writeEnable();
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(_fourByte ? SNOR_CMD_SE4B : SNOR_CMD_SE);
  sendAddr(addr);
  csHigh();
  SPI.endTransaction();
  return waitWip(5000, err);
}

bool SpiNor::chipErase(String &err) {
  writeEnable();
  SPI.beginTransaction(g_spiSettings);
  csLow();
  SPI.transfer(SNOR_CMD_CE);
  csHigh();
  SPI.endTransaction();
  // Chip erase poate dura zeci de secunde pe cip-uri mari.
  return waitWip(300000UL, err);
}

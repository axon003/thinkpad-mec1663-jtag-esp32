/*
 * spi_nor.h - driver SPI NOR 25-series (W25Q, MX25, etc.)
 * Versiune: 1.0
 *
 * STATUS: driver standard, de incredere. Suporta adresare pe 3 si 4 bytes
 * (>16MB, ex W25Q256 - cipul BIOS extern U5502 al T490). Foloseste hardware
 * SPI (VSPI) al ESP32.
 */

#ifndef MEMPROG_SPI_NOR_H
#define MEMPROG_SPI_NOR_H

#include <Arduino.h>

// Comenzi standard 25-series.
#define SNOR_CMD_RDID       0x9F  // JEDEC ID
#define SNOR_CMD_READ       0x03  // read (3-byte addr)
#define SNOR_CMD_READ4B     0x13  // read (4-byte addr)
#define SNOR_CMD_PP         0x02  // page program (3-byte addr)
#define SNOR_CMD_PP4B       0x12  // page program (4-byte addr)
#define SNOR_CMD_WREN       0x06  // write enable
#define SNOR_CMD_WRDI       0x04  // write disable
#define SNOR_CMD_RDSR       0x05  // read status reg 1
#define SNOR_CMD_SE         0x20  // sector erase 4KB (3-byte)
#define SNOR_CMD_SE4B       0x21  // sector erase 4KB (4-byte)
#define SNOR_CMD_CE         0xC7  // chip erase
#define SNOR_CMD_EN4B       0xB7  // enter 4-byte address mode
#define SNOR_CMD_EX4B       0xE9  // exit 4-byte address mode

#define SNOR_SR_WIP         0x01  // write-in-progress
#define SNOR_SR_WEL         0x02  // write-enable-latch

#define SNOR_PAGE_SIZE      256
#define SNOR_SECTOR_SIZE    4096

struct SpiNorId {
  uint8_t manufacturer;
  uint8_t memType;
  uint8_t capacity;      // 2^capacity bytes de obicei
  uint32_t sizeBytes;    // dedus din capacity (0 daca necunoscut)
};

class SpiNor {
public:
  // Idempotent: prima chemare initializeaza VSPI, urmatoarele nu fac nimic.
  // ATENTIE placa C-27J: begin() ocupa GPIO 18/19/23/5 (VSPI), care se suprapun
  // cu JTAG TDI(18) si TFT CS2(23). De aceea NU se apeleaza la boot, ci LAZY,
  // doar cand se cere efectiv o operatie SPI (pe placa dedicata).
  void begin();
  void end();      // v2.0: elibereaza VSPI ca begin() sa poata relua cu alti pini (SET pin.*)

  // Seteaza adresarea pe 4 bytes (necesar peste 16MB). true = 4-byte.
  void set4ByteMode(bool enable);
  bool is4Byte() const { return _fourByte; }

  SpiNorId readId();

  // Citeste 'len' bytes de la 'addr' in buf.
  void read(uint32_t addr, uint8_t *buf, uint32_t len);

  // Programeaza 'len' bytes la 'addr' (respecta limitele de pagina 256B intern).
  // Zona trebuie sa fie stearsa (0xFF) in prealabil.
  bool program(uint32_t addr, const uint8_t *buf, uint32_t len, String &err);

  bool sectorErase(uint32_t addr, String &err);
  bool chipErase(String &err);

private:
  bool _begun = false;
  bool _fourByte = false;

  void csLow();
  void csHigh();
  void writeEnable();
  uint8_t readStatus();
  bool waitWip(uint32_t timeoutMs, String &err);
  void sendAddr(uint32_t addr);     // 3 sau 4 bytes dupa mod
  bool pageProgram(uint32_t addr, const uint8_t *buf, uint32_t len, String &err);
};

#endif // MEMPROG_SPI_NOR_H

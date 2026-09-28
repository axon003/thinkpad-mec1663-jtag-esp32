/*
 * mec16xx.cpp - implementare driver MEC16xx (portat FIDEL din mec16xx.py)
 * Versiune: 1.0
 */

#include "mec16xx.h"
#include "config.h"

// Limita de siguranta pentru buclele Busy/Data_Full (evitam blocaj infinit).
#define MEC_POLL_MAX  200000

bool Mec16xx::begin(ArcId &id, String &err) {
  id = _arc.identify();
  if (!id.is_arc6xx) {
    err = "Device necunoscut / nu e ARC6xx (IDCODE=0x" + String(id.raw, HEX) + ")";
    return false;
  }
  // Halteaza CPU inainte de orice acces la memorie (ca in MEC16xxInterface).
  if (!_arc.setHalted(true, err)) return false;
  return true;
}

bool Mec16xx::enableFlashAccess(bool enabled, String &err) {
  // Flash_Config = Reg_Ctl_En (doar acest bit), ca in enable_flash_access.
  uint32_t cfg = enabled ? FC_REG_CTL_EN : 0;
  return wr(MEC_FLASH_CONFIG_ADDR, cfg, err);
}

bool Mec16xx::readFlashStatus(uint32_t &status, String &err) {
  return rd(MEC_FLASH_STATUS_ADDR, status, err);
}

bool Mec16xx::flashCleanStart(String &err) {
  // Reg_Ctl=1 + Flash_Mode=Standby (aduce controllerul in standby).
  if (!wr(MEC_FLASH_COMMAND_ADDR, FLASH_CMD_REG_CTL | FLASH_MODE_STANDBY, err)) return false;
  // Curata erorile: Busy_Err | CMD_Err | Protect_Err.
  if (!wr(MEC_FLASH_STATUS_ADDR, FS_BUSY_ERR | FS_CMD_ERR | FS_PROTECT_ERR, err)) return false;
  return true;
}

bool Mec16xx::flashWaitNotBusy(String &err) {
  for (uint32_t i = 0; i < MEC_POLL_MAX; i++) {
    uint32_t st;
    if (!rd(MEC_FLASH_STATUS_ADDR, st, err)) return false;
    if (st & (FS_BUSY_ERR | FS_CMD_ERR | FS_PROTECT_ERR)) {
      err = "Flash command FAILED, status=0x" + String(st, HEX);
      return false;
    }
    if (!(st & FS_BUSY)) return true;
  }
  err = "Flash BUSY timeout";
  return false;
}

bool Mec16xx::flashWaitDataNotFull(String &err) {
  for (uint32_t i = 0; i < MEC_POLL_MAX; i++) {
    uint32_t st;
    if (!rd(MEC_FLASH_STATUS_ADDR, st, err)) return false;
    if (st & (FS_BUSY_ERR | FS_CMD_ERR | FS_PROTECT_ERR)) {
      err = "Flash program FAILED, status=0x" + String(st, HEX);
      return false;
    }
    if (!(st & FS_DATA_FULL)) return true;
  }
  err = "Flash Data_Full timeout";
  return false;
}

bool Mec16xx::flashCommand(uint8_t mode, uint32_t address, bool burst, String &err) {
  uint32_t cmd = FLASH_CMD_REG_CTL | (uint32_t)mode;
  if (burst) cmd |= FLASH_CMD_BURST;
  if (!wr(MEC_FLASH_COMMAND_ADDR, cmd, err)) return false;
  if (mode != FLASH_MODE_STANDBY) {
    if (!wr(MEC_FLASH_ADDRESS_ADDR, address, err)) return false;
  }
  return flashWaitNotBusy(err);
}

// Portare fidela a read_flash: pentru fiecare word citim de 2 (si la nevoie 3)
// ori si alegem prin majoritate, pentru ca read-ul e "glitchy" (uneori 0 fals).
bool Mec16xx::readFlashWordMajority(uint32_t byteAddr, uint32_t &out, String &err) {
  uint32_t d1, d2;
  if (!flashCommand(FLASH_MODE_READ, byteAddr, false, err)) return false;
  if (!rd(MEC_FLASH_DATA_ADDR, d1, err)) return false;

  // A doua citire: re-scriem adresa si recitim Flash_Data (vezi comentariul
  // din mec16xx.py despre bug-ul de silicon la burst/ack).
  if (!wr(MEC_FLASH_ADDRESS_ADDR, byteAddr, err)) return false;
  if (!rd(MEC_FLASH_DATA_ADDR, d2, err)) return false;

  if (d1 == d2) { out = d1; return true; }

  // A treia oara (majoritate).
  uint32_t d3;
  if (!wr(MEC_FLASH_ADDRESS_ADDR, byteAddr, err)) return false;
  if (!rd(MEC_FLASH_DATA_ADDR, d3, err)) return false;

  if (d1 == d2)      out = d1;
  else if (d2 == d3) out = d2;
  else if (d1 == d3) out = d3;
  else { err = "read glitch: nu pot alege prin majoritate @0x" + String(byteAddr, HEX); return false; }
  return true;
}

bool Mec16xx::readFlash(uint32_t byteAddr, uint32_t countWords,
                        bool (*sink)(uint32_t word), String &err) {
  if (!flashCleanStart(err)) return false;
  for (uint32_t i = 0; i < countWords; i++) {
    uint32_t w;
    if (!readFlashWordMajority(byteAddr + i * 4, w, err)) {
      flashCommand(FLASH_MODE_STANDBY, 0, false, err); // best-effort standby
      return false;
    }
    if (sink && !sink(w)) { err = "sink abort"; return false; }
  }
  return flashCommand(FLASH_MODE_STANDBY, 0, false, err);
}

bool Mec16xx::erasePage(uint32_t byteAddr, String &err) {
  if (!flashCleanStart(err)) return false;
  // Erase modul cu adresa in pagina => sterge acea pagina (2048B).
  if (!flashCommand(FLASH_MODE_ERASE, byteAddr, false, err)) return false;
  return flashCommand(FLASH_MODE_STANDBY, 0, false, err);
}

bool Mec16xx::programFlash(uint32_t byteAddr, const uint32_t *words, uint32_t countWords, String &err) {
  if (!flashCleanStart(err)) return false;
  if (!flashCommand(FLASH_MODE_PROGRAM, byteAddr, true, err)) return false; // burst=1
  for (uint32_t i = 0; i < countWords; i++) {
    if (!flashWaitDataNotFull(err)) return false;
    if (!wr(MEC_FLASH_DATA_ADDR, words[i], err)) return false;
  }
  if (!flashWaitNotBusy(err)) return false;
  return flashCommand(FLASH_MODE_STANDBY, 0, false, err);
}

// ---------------------------------------------------------------------------
// EEPROM
// ---------------------------------------------------------------------------
bool Mec16xx::isEepromBlocked(bool &blocked, String &err) {
  uint32_t st;
  if (!rd(MEC_EEPROM_STATUS_ADDR, st, err)) return false;
  blocked = (st & ES_EEPROM_BLOCK) != 0;
  return true;
}

bool Mec16xx::eepromCleanStart(String &err) {
  bool blocked;
  if (!isEepromBlocked(blocked, err)) return false;
  if (blocked) { err = "EEPROM este blocat; nicio operatie EEPROM posibila"; return false; }
  if (!wr(MEC_EEPROM_COMMAND_ADDR, EEPROM_MODE_STANDBY, err)) return false;
  if (!wr(MEC_EEPROM_STATUS_ADDR, ES_BUSY_ERR | ES_CMD_ERR, err)) return false;
  return true;
}

bool Mec16xx::eepromWaitNotBusy(String &err) {
  for (uint32_t i = 0; i < MEC_POLL_MAX; i++) {
    uint32_t st;
    if (!rd(MEC_EEPROM_STATUS_ADDR, st, err)) return false;
    if (st & (ES_BUSY_ERR | ES_CMD_ERR)) {
      err = "EEPROM command FAILED, status=0x" + String(st, HEX);
      return false;
    }
    if (!(st & ES_BUSY)) return true;
  }
  err = "EEPROM BUSY timeout";
  return false;
}

bool Mec16xx::eepromWaitDataNotFull(String &err) {
  for (uint32_t i = 0; i < MEC_POLL_MAX; i++) {
    uint32_t st;
    if (!rd(MEC_EEPROM_STATUS_ADDR, st, err)) return false;
    if (st & (ES_BUSY_ERR | ES_CMD_ERR)) {
      err = "EEPROM program FAILED, status=0x" + String(st, HEX);
      return false;
    }
    if (!(st & ES_DATA_FULL)) return true;
  }
  err = "EEPROM Data_Full timeout";
  return false;
}

bool Mec16xx::eepromCommand(uint8_t mode, uint32_t address, bool burst, String &err) {
  uint32_t cmd = (uint32_t)mode;
  if (burst) cmd |= EEPROM_CMD_BURST;
  if (!wr(MEC_EEPROM_COMMAND_ADDR, cmd, err)) return false;
  if (mode != EEPROM_MODE_STANDBY) {
    if (!wr(MEC_EEPROM_ADDRESS_ADDR, address, err)) return false;
  }
  return eepromWaitNotBusy(err);
}

bool Mec16xx::readEeprom(uint32_t address, uint32_t count,
                         bool (*sink)(uint8_t b), String &err) {
  if (!eepromCleanStart(err)) return false;
  if (!eepromCommand(EEPROM_MODE_READ, address, true, err)) return false; // burst
  for (uint32_t i = 0; i < count; i++) {
    uint32_t d;
    if (!rd(MEC_EEPROM_DATA_ADDR, d, err)) return false;
    if (sink && !sink((uint8_t)(d & 0xFF))) { err = "sink abort"; return false; }
  }
  return eepromCommand(EEPROM_MODE_STANDBY, 0, false, err);
}

bool Mec16xx::eraseEepromAll(String &err) {
  if (!eepromCleanStart(err)) return false;
  // 0b11111 << 11 = numar magic: sterge tot EEPROM-ul (vezi erase_eeprom).
  if (!eepromCommand(EEPROM_MODE_ERASE, (uint32_t)0x1F << 11, false, err)) return false;
  return eepromCommand(EEPROM_MODE_STANDBY, 0, false, err);
}

bool Mec16xx::programEeprom(uint32_t address, const uint8_t *data, uint32_t count, String &err) {
  if (!eepromCleanStart(err)) return false;
  if (!eepromCommand(EEPROM_MODE_PROGRAM, address, true, err)) return false;
  for (uint32_t i = 0; i < count; i++) {
    if (!eepromWaitDataNotFull(err)) return false;
    if (!wr(MEC_EEPROM_DATA_ADDR, data[i], err)) return false;
  }
  if (!eepromWaitNotBusy(err)) return false;
  return eepromCommand(EEPROM_MODE_STANDBY, 0, false, err);
}

bool Mec16xx::unlockEeprom(uint32_t password, String &err) {
  bool blocked;
  if (!isEepromBlocked(blocked, err)) return false;
  if (!blocked) { err = "EEPROM nu e blocat; nimic de deblocat"; return false; }
  if (!wr(MEC_EEPROM_UNLOCK_ADDR, password, err)) return false;
  if (!isEepromBlocked(blocked, err)) return false;
  if (blocked) { err = "EEPROM NU a fost deblocat (parola gresita?)"; return false; }
  return true;
}

// ---------------------------------------------------------------------------
// Emergency mass erase - PORTAT FIDEL din emergency_mass_erase().
// Secventa scrie DR_RESET_TEST prin IR_RESET_TEST (registru JTAG, NU memorie).
// ATENTIE: sterge tot flash + eeprom, inclusiv serial/MAC.
// ---------------------------------------------------------------------------
bool Mec16xx::emergencyMassErase(String &err) {
  Jtag &tap = _arc.tap();

  // Toate scrierile de DR sunt pe registrul selectat de IR_RESET_TEST.
  tap.writeIR(ARC_IR_RESET_TEST, ARC_IR_BITS);

  uint32_t dr = RT_VTR_POR | RT_VCC_POR;         // init in stare dezasertata
  tap.writeDR(dr, 32);

  dr |= RT_POR_EN;      tap.writeDR(dr, 32);
  dr &= ~RT_VTR_POR;    tap.writeDR(dr, 32);
  dr |= RT_ME;          tap.writeDR(dr, 32);
  dr |= RT_VTR_POR;     tap.writeDR(dr, 32);

  // In practica 0.1s ajunge; asteptam 1s ca in sursa.
  delay(1000);

  dr &= ~RT_ME;         tap.writeDR(dr, 32);
  dr &= ~RT_VTR_POR;    tap.writeDR(dr, 32);
  dr |= RT_VTR_POR;     tap.writeDR(dr, 32);
  dr &= ~RT_POR_EN;     tap.writeDR(dr, 32);

  // Nu avem un status direct de succes aici (secventa e "oarba"); un power
  // cycle poate fi necesar pe unele cipuri. Semnalam ca a rulat.
  (void)err;
  return true;
}

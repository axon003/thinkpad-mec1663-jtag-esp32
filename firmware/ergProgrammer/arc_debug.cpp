/*
 * arc_debug.cpp - implementare interfata debug ARC (portat FIDEL din debug_arc.py)
 * Versiune: 1.0
 */

#include "arc_debug.h"
#include "config.h"

// Numar maxim de poll-uri pe DR_STATUS inainte sa raportam timeout.
#define ARC_TXN_POLL_MAX  100000

uint8_t ArcDebug::txnForRead(ArcSpace s) {
  switch (s) {
    case ARC_SPACE_MEMORY: return ARC_TXN_READ_MEMORY;
    case ARC_SPACE_CORE:   return ARC_TXN_READ_CORE;
    case ARC_SPACE_AUX:    return ARC_TXN_READ_AUX;
  }
  return ARC_TXN_READ_MEMORY;
}

uint8_t ArcDebug::txnForWrite(ArcSpace s) {
  switch (s) {
    case ARC_SPACE_MEMORY: return ARC_TXN_WRITE_MEMORY;
    case ARC_SPACE_CORE:   return ARC_TXN_WRITE_CORE;
    case ARC_SPACE_AUX:    return ARC_TXN_WRITE_AUX;
  }
  return ARC_TXN_WRITE_MEMORY;
}

ArcId ArcDebug::identify() {
  ArcId id;
  _jtag.testReset();
  // Dupa Test-Logic-Reset, IR = IDCODE si DR = IDCODE; citim 32b.
  id.raw = _jtag.readDR(32);
  // Format JTAG IDCODE standard (glasgow DR_IDCODE):
  //  bit0 = present(1), bits1..11 = mfg_id, bits12..27 = part_id, bits28..31 = version
  id.mfg_id  = (uint16_t)((id.raw >> 1) & 0x7FF);
  id.part_id = (uint16_t)((id.raw >> 12) & 0xFFFF);
  id.version = (uint8_t)((id.raw >> 28) & 0xF);
  id.is_arc6xx = (id.mfg_id == ARC_MFG_ID) && ((id.part_id & 0x3F) == ARC_PART_ARC6XX);
  id.is_arc7xx = (id.mfg_id == ARC_MFG_ID) && ((id.part_id & 0x3F) == ARC_PART_ARC7XX);
  return id;
}

bool ArcDebug::waitTxn(String &err) {
  _jtag.writeIR(ARC_IR_STATUS, ARC_IR_BITS);
  for (uint32_t i = 0; i < ARC_TXN_POLL_MAX; i++) {
    uint32_t st = _jtag.readDR(4);
    bool fl = (st >> ARC_STATUS_FL_BIT) & 1;
    bool rd = (st >> ARC_STATUS_RD_BIT) & 1;
    if (fl) {
      err = "ARC transaction FAILED (DR_STATUS.FL=1, status=0x" + String(st, HEX) + ")";
      return false;
    }
    if (rd) return true;
  }
  err = "ARC transaction TIMEOUT (RD nu s-a ridicat)";
  return false;
}

bool ArcDebug::read(uint32_t address, ArcSpace space, uint32_t &out, String &err) {
  uint8_t cmd = txnForRead(space);
  // IR_ADDRESS -> DR addr(32) ; IR_TXN_COMMAND -> DR cmd(4) ; puls RTI ; wait ; IR_DATA -> read 32
  _jtag.writeIR(ARC_IR_ADDRESS, ARC_IR_BITS);
  _jtag.writeDR(address, 32);
  _jtag.writeIR(ARC_IR_TXN_COMMAND, ARC_IR_BITS);
  _jtag.writeDR(cmd, ARC_TXN_BITS);
  _jtag.runTestIdle(1);              // QUIRK ARC: tranzactia se initiaza aici
  if (!waitTxn(err)) return false;
  _jtag.writeIR(ARC_IR_DATA, ARC_IR_BITS);
  out = _jtag.readDR(32);
  return true;
}

bool ArcDebug::write(uint32_t address, uint32_t data, ArcSpace space, String &err) {
  uint8_t cmd = txnForWrite(space);
  _jtag.writeIR(ARC_IR_ADDRESS, ARC_IR_BITS);
  _jtag.writeDR(address, 32);
  _jtag.writeIR(ARC_IR_DATA, ARC_IR_BITS);
  _jtag.writeDR(data, 32);
  _jtag.writeIR(ARC_IR_TXN_COMMAND, ARC_IR_BITS);
  _jtag.writeDR(cmd, ARC_TXN_BITS);
  _jtag.runTestIdle(1);              // QUIRK ARC: tranzactia se initiaza aici
  return waitTxn(err);
}

bool ArcDebug::isHalted(bool &halted, String &err) {
  uint32_t s32;
  if (!read(AUX_STATUS32_ADDR, ARC_SPACE_AUX, s32, err)) return false;
  halted = (s32 >> AUX_STATUS32_H_BIT) & 1;
  return true;
}

bool ArcDebug::forceHalt(String &err) {
  // read-modify-write pe AUX_DEBUG, seteaza FH=1 apoi FH=0 (ca in debug_arc.py).
  uint32_t dbg;
  if (!read(AUX_DEBUG_ADDR, ARC_SPACE_AUX, dbg, err)) return false;
  uint32_t withFH = dbg | ((uint32_t)1 << AUX_DEBUG_FH_BIT);
  if (!write(AUX_DEBUG_ADDR, withFH, ARC_SPACE_AUX, err)) return false;
  uint32_t clrFH = withFH & ~((uint32_t)1 << AUX_DEBUG_FH_BIT);
  if (!write(AUX_DEBUG_ADDR, clrFH, ARC_SPACE_AUX, err)) return false;
  return true;
}

bool ArcDebug::setHalted(bool halted, String &err) {
  bool cur;
  if (!isHalted(cur, err)) return false;
  if (cur == halted) return true;
  if (halted) {
    if (!forceHalt(err)) return false;
    if (!isHalted(cur, err)) return false;
    if (!cur) { err = "Halting the ARC failed!"; return false; }
    return true;
  } else {
    uint32_t s32;
    if (!read(AUX_STATUS32_ADDR, ARC_SPACE_AUX, s32, err)) return false;
    s32 &= ~((uint32_t)1 << AUX_STATUS32_H_BIT);
    if (!write(AUX_STATUS32_ADDR, s32, ARC_SPACE_AUX, err)) return false;
    if (!isHalted(cur, err)) return false;
    if (cur) { err = "Un-halting the ARC failed!"; return false; }
    return true;
  }
}

/*
 * arc_debug.h - interfata de debug ARC prin JTAG (portat din debug_arc.py / arc_jtag.py / arc_core.py)
 * Versiune: 1.0
 *
 * Registrii (IR opcodes, DR_TXN_COMMAND, AUX) sunt transpusi FIDEL din sursa
 * Glasgow. Nu inventa valori aici.
 */

#ifndef MEMPROG_ARC_DEBUG_H
#define MEMPROG_ARC_DEBUG_H

#include <Arduino.h>
#include "jtag.h"

// ---- IR opcodes (arc_jtag.py) : 4 biti ----
#define ARC_IR_RESET_TEST   0x2   // 0b0010  DR[32]
#define ARC_IR_STATUS       0x8   // 0b1000  DR[4]
#define ARC_IR_TXN_COMMAND  0x9   // 0b1001  DR[4]
#define ARC_IR_ADDRESS      0xA   // 0b1010  DR[32]
#define ARC_IR_DATA         0xB   // 0b1011  DR[32]
#define ARC_IR_IDCODE       0xC   // 0b1100  DR[32]
#define ARC_IR_BYPASS       0xF   // 0b1111  DR[1]
#define ARC_IR_BITS         4

// ---- DR_STATUS (4 biti) : ST, FL, RD, PC_SEL ----
#define ARC_STATUS_ST_BIT     0
#define ARC_STATUS_FL_BIT     1
#define ARC_STATUS_RD_BIT     2
#define ARC_STATUS_PCSEL_BIT  3

// ---- DR_TXN_COMMAND (4 biti) ----
#define ARC_TXN_WRITE_MEMORY  0x0  // 0b0000
#define ARC_TXN_WRITE_CORE    0x1  // 0b0001
#define ARC_TXN_WRITE_AUX     0x2  // 0b0010
#define ARC_TXN_READ_MEMORY   0x4  // 0b0100
#define ARC_TXN_READ_CORE     0x5  // 0b0101
#define ARC_TXN_READ_AUX      0x6  // 0b0110
#define ARC_TXN_BITS          4

// ---- AUX regs (arc_core.py) ----
#define AUX_IDENTITY_ADDR   0x04
#define AUX_DEBUG_ADDR      0x05
#define AUX_PC_ADDR         0x06
#define AUX_STATUS32_ADDR   0x0a
// AUX_STATUS32.H = bit0 (halted); AUX_DEBUG.FH = bit1 (force halt)
#define AUX_STATUS32_H_BIT  0
#define AUX_DEBUG_FH_BIT    1

// ---- ID device (db_arc.py) : ARC6xx = (mfg 0x258, part&0x3f = 0x02) ----
#define ARC_MFG_ID          0x258
#define ARC_PART_ARC6XX     0x02
#define ARC_PART_ARC7XX     0x03

enum ArcSpace { ARC_SPACE_MEMORY, ARC_SPACE_CORE, ARC_SPACE_AUX };

struct ArcId {
  uint32_t raw;
  uint16_t mfg_id;    // 11 biti
  uint16_t part_id;   // 16 biti
  uint8_t  version;   // 4 biti
  bool     is_arc6xx;
  bool     is_arc7xx;
};

class ArcDebug {
public:
  explicit ArcDebug(Jtag &jtag) : _jtag(jtag) {}

  // test_reset + citeste IDCODE (32b). Descompune campurile JTAG standard.
  ArcId identify();

  // Read/write pe un spatiu (memory/core/aux). Intoarce true la succes.
  // 'err' primeste mesaj daca tranzactia esueaza (status FL) sau timeout.
  bool read(uint32_t address, ArcSpace space, uint32_t &out, String &err);
  bool write(uint32_t address, uint32_t data, ArcSpace space, String &err);

  // Halt CPU (force_halt: seteaza AUX_DEBUG.FH=1 apoi 0). Verifica STATUS32.H.
  bool forceHalt(String &err);
  bool isHalted(bool &halted, String &err);
  bool setHalted(bool halted, String &err);

  // Acces direct la TAP pentru secventele de reset (emergency mass erase).
  Jtag &tap() { return _jtag; }

private:
  Jtag &_jtag;
  // Asteapta finalizarea tranzactiei: IR_STATUS, poll DR_STATUS pana RD=1.
  // FL=1 => esec. Timeout de siguranta ca sa nu blocam la infinit.
  bool waitTxn(String &err);
  uint8_t txnForRead(ArcSpace s);
  uint8_t txnForWrite(ArcSpace s);
};

#endif // MEMPROG_ARC_DEBUG_H

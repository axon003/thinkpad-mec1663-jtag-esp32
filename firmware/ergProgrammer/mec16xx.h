/*
 * mec16xx.h - driver Microchip MEC16xx (flash + eeprom embedded) prin JTAG/ARC
 * Versiune: 1.0
 *
 * Registrii transpusi FIDEL din arch_mec.py. Logica flash/eeprom portata din
 * mec16xx.py (Glasgow program-mec16xx). Include quirk-ul de citire "glitchy"
 * (majoritate 2/3) si granularitatea de erase pe pagina de 2048 bytes.
 *
 * STATUS: PORTAT, NEVALIDAT PE BANC. Inainte de orice erase/write pe placa
 * reala: intai MECID (verifica ARC6xx) + MECRD (dump) si confirma ca datele
 * arata sensibil. Un offset gresit + write poate brica placa.
 */

#ifndef MEMPROG_MEC16XX_H
#define MEMPROG_MEC16XX_H

#include <Arduino.h>
#include "arc_debug.h"

// ---- Dimensiuni (mec16xx.py) ----
#define MEC_FLASH_SIZE_MAX   0x40000UL   // 256 KiB (MEC1663)
#define MEC_EEPROM_SIZE      2048UL      // 2 KiB
#define MEC_FLASH_PAGE_SIZE  2048UL      // granularitate erase

// ---- Flash controller (arch_mec.py) : base 0xff3800 ----
#define MEC_FLASH_BASE       0xff3800UL
#define MEC_FLASH_DATA_ADDR     (MEC_FLASH_BASE + 0x100)
#define MEC_FLASH_ADDRESS_ADDR  (MEC_FLASH_BASE + 0x104)
#define MEC_FLASH_COMMAND_ADDR  (MEC_FLASH_BASE + 0x108)
#define MEC_FLASH_STATUS_ADDR   (MEC_FLASH_BASE + 0x10c)
#define MEC_FLASH_CONFIG_ADDR   (MEC_FLASH_BASE + 0x110)
#define MEC_FLASH_INIT_ADDR     (MEC_FLASH_BASE + 0x114)

// Flash_Command bitfields: Flash_Mode[1:0], Burst[2], EC_Int[3], Reg_Ctl[8]
#define FLASH_MODE_STANDBY   0
#define FLASH_MODE_READ      1
#define FLASH_MODE_PROGRAM   2
#define FLASH_MODE_ERASE     3
#define FLASH_CMD_BURST      (1UL << 2)
#define FLASH_CMD_REG_CTL    (1UL << 8)

// Flash_Status bitfields
#define FS_BUSY          (1UL << 0)
#define FS_DATA_FULL     (1UL << 1)
#define FS_ADDRESS_FULL  (1UL << 2)
#define FS_BOOT_LOCK     (1UL << 3)
#define FS_BOOT_BLOCK    (1UL << 5)
#define FS_DATA_BLOCK    (1UL << 6)
#define FS_EEPROM_BLOCK  (1UL << 7)
#define FS_BUSY_ERR      (1UL << 8)
#define FS_CMD_ERR       (1UL << 9)
#define FS_PROTECT_ERR   (1UL << 10)

// Flash_Config bitfields
#define FC_REG_CTL_EN        (1UL << 0)
#define FC_HOST_CTL          (1UL << 1)
#define FC_BOOT_LOCK         (1UL << 2)
#define FC_BOOT_PROTECT_EN   (1UL << 3)
#define FC_DATA_PROTECT      (1UL << 4)
#define FC_INHIBIT_JTAG      (1UL << 5)
#define FC_EEPROM_ACCESS     (1UL << 8)
#define FC_EEPROM_PROTECT    (1UL << 9)
#define FC_EEPROM_FORCE_BLOCK (1UL << 10)

// ---- EEPROM controller (arch_mec.py) : base 0xf02c00 ----
#define MEC_EEPROM_BASE          0xf02c00UL
#define MEC_EEPROM_DATA_ADDR     (MEC_EEPROM_BASE + 0x00)
#define MEC_EEPROM_ADDRESS_ADDR  (MEC_EEPROM_BASE + 0x04)
#define MEC_EEPROM_COMMAND_ADDR  (MEC_EEPROM_BASE + 0x08)
#define MEC_EEPROM_STATUS_ADDR   (MEC_EEPROM_BASE + 0x0c)
#define MEC_EEPROM_CONFIG_ADDR   (MEC_EEPROM_BASE + 0x10)
#define MEC_EEPROM_UNLOCK_ADDR   (MEC_EEPROM_BASE + 0x20)

#define EEPROM_MODE_STANDBY  0
#define EEPROM_MODE_READ     1
#define EEPROM_MODE_PROGRAM  2
#define EEPROM_MODE_ERASE    3
#define EEPROM_CMD_BURST     (1UL << 2)

// EEPROM_Status bitfields
#define ES_BUSY          (1UL << 0)
#define ES_DATA_FULL     (1UL << 1)
#define ES_ADDRESS_FULL  (1UL << 2)
#define ES_EEPROM_BLOCK  (1UL << 7)
#define ES_BUSY_ERR      (1UL << 8)
#define ES_CMD_ERR       (1UL << 9)

// ---- DR_RESET_TEST bits (arch_mec.py) - pe registrul JTAG IR_RESET_TEST ----
#define RT_ME       (1UL << 0)
#define RT_VCC_POR  (1UL << 1)
#define RT_VTR_POR  (1UL << 2)
#define RT_POR_EN   (1UL << 3)
#define RT_GANG_EN  (1UL << 31)

class Mec16xx {
public:
  explicit Mec16xx(ArcDebug &arc) : _arc(arc) {}

  // Verifica ARC6xx si halteaza CPU (ca in MEC16xxInterface.__init__).
  bool begin(ArcId &id, String &err);

  // Activeaza/dezactiveaza accesul la regii controllerului flash (Reg_Ctl_En).
  bool enableFlashAccess(bool enabled, String &err);

  // Citeste un word de flash cu majoritate anti-glitch (2/3). Presupune ca
  // enableFlashAccess(true) + _flashCleanStart au fost apelate (vezi readFlash).
  bool readFlashWordMajority(uint32_t byteAddr, uint32_t &out, String &err);

  // Citeste 'countWords' words incepand de la byteAddr (aliniat la 4).
  // Apeleaza callback pentru fiecare word citit (streaming spre host).
  bool readFlash(uint32_t byteAddr, uint32_t countWords,
                 bool (*sink)(uint32_t word), String &err);

  // Sterge pagina (2048B) care contine 'byteAddr'.
  bool erasePage(uint32_t byteAddr, String &err);

  // Programeaza 'countWords' words la byteAddr. Presupune zona deja stearsa.
  bool programFlash(uint32_t byteAddr, const uint32_t *words, uint32_t countWords, String &err);

  // Citeste statusul flash brut (pentru security-status).
  bool readFlashStatus(uint32_t &status, String &err);

  // ---- EEPROM ----
  bool isEepromBlocked(bool &blocked, String &err);
  bool readEeprom(uint32_t address, uint32_t count,
                  bool (*sink)(uint8_t b), String &err);
  bool eraseEepromAll(String &err);
  bool programEeprom(uint32_t address, const uint8_t *data, uint32_t count, String &err);
  bool unlockEeprom(uint32_t password, String &err);

  // ---- Emergency mass erase (PORTAT) ----
  // ATENTIE: sterge TOT flash-ul SI eeprom-ul, inclusiv serial/MAC/DMI/UUID.
  // NU e stergerea "chirurgicala" ceruta implicit. Apeleaza doar constient.
  bool emergencyMassErase(String &err);

private:
  ArcDebug &_arc;

  bool wr(uint32_t addr, uint32_t data, String &err) {
    return _arc.write(addr, data, ARC_SPACE_MEMORY, err);
  }
  bool rd(uint32_t addr, uint32_t &out, String &err) {
    return _arc.read(addr, ARC_SPACE_MEMORY, out, err);
  }

  bool flashCleanStart(String &err);
  bool flashCommand(uint8_t mode, uint32_t address, bool burst, String &err);
  bool flashWaitNotBusy(String &err);
  bool flashWaitDataNotFull(String &err);

  bool eepromCleanStart(String &err);
  bool eepromCommand(uint8_t mode, uint32_t address, bool burst, String &err);
  bool eepromWaitNotBusy(String &err);
  bool eepromWaitDataNotFull(String &err);
};

#endif // MEMPROG_MEC16XX_H

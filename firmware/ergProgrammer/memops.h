/**
 * memops.h — operatiile de nivel inalt pe memoria ACTIVA (mem.type), cu fisiere pe url.base
 *   READ   -> citeste memoria si o trimite in flux la <url.base>/<nume> (MD5 la final)
 *   WRITE  -> ia <nume> de pe url.base si il programeaza (read-modify-write pe blocuri de
 *             erase: pagina 2 KB la MEC, sector 4 KB la SPI; I2C/MW direct), verificat prin recitire
 *   VERIFY -> compara memoria cu fisierul (nr diferente + prima adresa)
 *   ERASE  -> chip / bloc la adresa
 *   CLEAR  -> stergere CHIRURGICALA MEC (backup integral pe server, apoi doar paginile atinse
 *             sunt sterse si rescrise din backup cu zona tinta umpluta) - reset parola SVP T490
 *   Toate raporteaza progres cu linii "# ..." si intorc false + motiv la orice esec.
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0; inlocuieste opSurgical/download-urile din web_ui 1.0)
 */
#ifndef MEMPROG_MEMOPS_H
#define MEMPROG_MEMOPS_H

#include <Arduino.h>

enum MemType { MT_NONE = 0, MT_MEC, MT_MECEE, MT_SPI, MT_I2C, MT_MW };

MemType     memCurrent();
const char *memName(MemType t);
MemType     memParse(const char *s);
bool        memSelect(const char *name, String &err);   // SET mem.type + reinit
void        memReinit();                                // dupa SET pin.* / spi.* / i2c.* / mw.*
bool        memId(String &out, String &err);
bool        memSize(uint32_t &size, String &err);       // mem.size sau implicit/ID
uint32_t    memBlock(MemType t);                        // granularitatea de erase (1 = fara erase)

typedef bool (*MemSink)(const uint8_t *d, size_t n);
bool memRead(uint32_t addr, uint32_t len, MemSink sink, String &err);
bool memReadToNet(uint32_t addr, uint32_t len, const String &name, String &md5, String &err);
bool memWriteFromNet(uint32_t addr, const String &name, String &err);
bool memVerifyNet(uint32_t addr, const String &name, uint32_t &bytes, uint32_t &diffs, uint32_t &firstDiff, String &err);
bool memErase(const String &what, uint32_t addr, String &err);
bool memClear(uint32_t off, uint32_t len, uint8_t fill, const String &backup, bool dryRun, String &err);

#endif // MEMPROG_MEMOPS_H

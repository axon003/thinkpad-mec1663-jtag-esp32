/**
 * cfg.h — setarile esp32_memprog in NVS (Preferences, namespace "memprog")
 *   Editabile din shell: CFG (lista), SET <cheie> <val>, RESET <cheie>, FACTORY.
 *   Chei max 15 caractere (limita NVS). Secretele (tip 'p') sunt mascate la afisare.
 *   Acelasi idiom ca la ergCAN (cfg.h de acolo), ca sa nu am doua moduri de lucru.
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0: shell telnet, pini setabili, URL fisiere)
 */
#ifndef MEMPROG_CFG_H
#define MEMPROG_CFG_H

#include <Arduino.h>

struct CfgDef {
    const char *key;
    char        type;      // 's' text, 'i' numar, 'p' secret (mascat la afisare)
    const char *def;
    long        min, max;  // pt 'i'
    const char *help;
};

void          cfg_init();
const char   *cfg_str(const char *key);
long          cfg_int(const char *key);
// seteaza + salveaza in NVS; err primeste motivul refuzului (fara erori silent)
bool          cfg_set(const char *key, const char *val, String &err);
bool          cfg_reset_key(const char *key);
void          cfg_factory_reset();
size_t        cfg_count();
const CfgDef *cfg_def(size_t i);
bool          cfg_is_secret(const char *key);

#endif // MEMPROG_CFG_H

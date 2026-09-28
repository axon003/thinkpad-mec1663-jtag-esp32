/**
 * cmd.h — dispecerul de comenzi al shell-ului ergProgrammer (o linie -> raspuns la owner-ul curent)
 *   Raspunsuri: "OK [text]" / "ERR <motiv>" ca linie finala, "# ..." informativ, "D <hex>" date
 *   (comenzile de nivel jos). Aceleasi comenzi pe serial, telnet si raw.
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0)
 */
#ifndef MEMPROG_CMD_H
#define MEMPROG_CMD_H

#include <Arduino.h>

void cmdBegin();               // init drivere (dupa cfg_init + pinsLoad)
void cmdLine(char *line);      // proceseaza o linie (o modifica in loc: tokenizare)

#endif // MEMPROG_CMD_H

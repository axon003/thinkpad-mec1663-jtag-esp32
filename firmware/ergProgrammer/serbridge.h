/**
 * serbridge.h — consola seriala pentru servere/routere: punte telnet(2324) <-> UART2
 *   ser2net de buzunar: te legi cu `telnet ip 2324` (sau PuTTY raw) si esti direct pe consola
 *   tintei (Cisco/MikroTik/iDRAC/OpenWrt). Comenzile din shell: SER ON|OFF|BREAK|DTR|RTS|LOG|STAT.
 *   Optional, tot ce vine de la tinta se scrie si intr-un fisier pe url.base (SER LOG boot.txt),
 *   in bucati, cu append - util la capturat log-ul de boot al unui router.
 *   Niveluri: UART-ul ESP32 e TTL 3.3V. RS232 (DB9/RJ45 Cisco) DOAR prin MAX3232 (vezi CHIPS).
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0)
 */
#ifndef MEMPROG_SERBRIDGE_H
#define MEMPROG_SERBRIDGE_H

#include <Arduino.h>

void   serbridgeBegin();                  // la boot: porneste UART-ul daca ser.on=1
void   serbridgeNetUp();                  // serverul 2324 (apelat de console la net up/down)
void   serbridgeNetDown();
void   serbridgeLoop();                   // pompeaza UART <-> clienti, flush log

bool   serbridgeStart(String &err);       // deschide UART cu ser.baud/ser.fmt/pin.stx/pin.srx
void   serbridgeStop();
bool   serbridgeActive();
bool   serbridgeBreak(String &err);       // BREAK ~250 ms (bootloader Cisco/MikroTik)
bool   serbridgeSetLine(const char *line, bool level, String &err);   // "DTR" / "RTS"
bool   serbridgeLogStart(const String &name, String &err);           // scrie in <url.base>/<name> (append)
bool   serbridgeLogStop(String &err);
String serbridgeStatus();

#endif // MEMPROG_SERBRIDGE_H

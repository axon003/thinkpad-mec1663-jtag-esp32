/**
 * console.h — retea + shell-ul ergProgrammer (ca la ergCAN)
 *   - WiFi: STA pe wifi.ssid; daca nu prinde, AP propriu de rezerva (wifi.ap=1) dupa
 *     WIFI_AP_FALLBACK_S; mDNS <dev.name>.local; serverele se releaga daca IP-ul se schimba.
 *   - Shell pe 3 cai, acelasi dispecer (cmd.h):
 *       serial USB     owner 0            (protocol linie-cu-linie, fara echo)
 *       telnet :23     owner 1..N         (echo, negociere IAC, backspace, prompt - PuTTY/ConnectBot)
 *       raw    :2323   owner N+1..        (fara echo/IAC - host/memprog.py --tcp)
 *   - Iesirea comenzilor se duce la owner-ul care a dat comanda (conPrint/conWrite).
 *     O singura comanda ruleaza la un moment dat (operatiile pe memorie dureaza minute);
 *     ceilalti clienti primesc "ocupat".
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial (FW 2.0)
 */
#ifndef MEMPROG_CONSOLE_H
#define MEMPROG_CONSOLE_H

#include <Arduino.h>

void conBegin();                 // WiFi + servere (dupa cfg_init)
void conLoop();                  // in loop(): WiFi tick, clienti, linii -> cmd

// --- iesire catre owner-ul curent ---
bool conWrite(const uint8_t *data, size_t n);   // false = clientul a plecat (opreste dump-ul)
bool conPrint(const String &s);
bool conPrintln(const String &s);
bool conPrintf(const char *fmt, ...);
int  conOwner();                                // owner-ul curent (0 = serial)
bool conOwnerIsHuman();                         // telnet (afisare prietenoasa) vs serial/raw (protocol)

// De apelat in bucle lungi (dump, program): tine WiFi/TFT vii si detecteaza deconectarea.
void conYield();

// --- stare retea (pt STAT) ---
String conIp();
String conWifiState();
bool   conNetUp();
void   conWifiRestart();        // dupa SET wifi.* : reconecteaza cu setarile noi

#endif // MEMPROG_CONSOLE_H

/*
 * ui_display.h - feedback pe 2x TFT ILI9341 (refolosite de pe placa C-27J)
 * Versiune: 1.0
 *
 * TFT1 = panou STARE (firmware, WiFi/IP, driver, operatie, progres, rezultat,
 *        device detectat). TFT2 = LOG live (ultimele linii, colorate pe tip).
 *
 * DESENAREA se face DOAR din context loop (uiTick), ca sa nu existe acces
 * concurent pe magistrala HSPI partajata cu task-ul de operatie. Task-ul de
 * operatie NU deseneaza; scrie doar in appstate, iar uiTick redeseneaza.
 *
 * Display OPTIONAL: daca TFT lipseste/nu raspunde, scrierile SPI sunt inofensive
 * si firmware-ul merge mai departe (uiPresent() spune ce s-a detectat).
 */

#ifndef MEMPROG_UI_DISPLAY_H
#define MEMPROG_UI_DISPLAY_H

#include <Arduino.h>

void uiBegin();
void uiTick();                 // apeleaza din loop() (si periodic in timpul dump-ului)
bool uiPresent(int idx);       // 0=TFT1, 1=TFT2 : true daca readID a raspuns
uint16_t uiReadId(int idx);    // pentru diagnoza

#endif // MEMPROG_UI_DISPLAY_H

/**
 * chips.cpp — catalogul de profile de tinta (vezi chips.h)
 * @author Claude Code
 * @version 1.0
 * @changes
 *   v1.0 2026-09-19 — creat initial
 *
 * Marcajele {TCK} {TMS} {TDI} {TDO} {TRST} {SCK} {MISO} {MOSI} {CS} {SDA} {SCL}
 * {MWCS} {MWSK} {MWDI} {MWDO} {STX} {SRX} {SDTR} {SRTS} din schema se inlocuiesc cu
 * pinii CURENTI din setari, ca sa nu apara o schema care nu corespunde cablajului.
 */
#include "chips.h"
#include "cfg.h"
#include "pins.h"

static const char W_T490_MEC[] =
"MEC1663 (EC) pe ThinkPad T490, placa NM-B901 - JTAG 5 fire, 3.3V, FARA level shifter\n"
"  semnal MEC      ESP32\n"
"  JTAG_CLK   ->   {TCK}\n"
"  JTAG_TMS   ->   {TMS}\n"
"  JTAG_TDI   ->   {TDI}   (ESP32 -> MEC)\n"
"  JTAG_TDO   ->   {TDO}   (MEC -> ESP32, pin input-only e ideal)\n"
"  JTAG_RST#  ->   {TRST}  (NECONECTAT la inceput: incearca ID/READ fara el; pe X230/MEC1619\n"
"                          documentatia spune ca uneori e necesar - abia atunci il tragi)\n"
"  GND        ->   GND ESP32 (OBLIGATORIU masa comuna)\n"
"Alimentare: placa T490 pe propria alimentare (baterie scoasa, adaptor in priza,\n"
"  laptop OPRIT dar cu 3.3V_ALW prezent). NU alimenta MEC-ul din ESP32.\n"
"NEVERIFICAT pe T490: pad-urile JTAG pe NM-B901 NU sunt documentate public si\n"
"  rezistorul jtag_rst nu a fost gasit. Referinta X230 (MEC1619): majoritatea pinilor\n"
"  sunt in conectorul CN27 (spate placa), pag. 62 din schema; RST se lipeste separat\n"
"  langa cip (fir emailat 0.1 mm). La T490 se urmaresc cu multimetrul de la pinii\n"
"  MEC1663 la pad-uri. Prima incercare: fara RST si fara dezlipit nimic; doar daca ID\n"
"  nu raspunde, gasesti punctul RST si faci SET pin.trst <gpio>.\n"
"Ordinea de lucru: ID (astept ARC6xx) -> READ backup.bin -> abia apoi ERASE/WRITE.\n";

static const char W_T490_BIOS[] =
"BIOS SPI NOR pe ThinkPad T490 (cip 25-series SOIC-8 / WSON-8, 3.3V)\n"
"  pin cip  semnal    ESP32\n"
"  1        CS#   ->  {CS}\n"
"  2        DO    ->  {MISO}  (cip -> ESP32)\n"
"  3        WP#   ->  3V3 (sau lasat cum e pe placa)\n"
"  4        GND   ->  GND ESP32\n"
"  5        DI    ->  {MOSI}  (ESP32 -> cip)\n"
"  6        CLK   ->  {SCK}\n"
"  7        HOLD# ->  3V3 (sau lasat cum e pe placa)\n"
"  8        VCC   ->  3V3 ESP32 doar daca placa e complet NEalimentata (in-circuit)\n"
"Punctul 1 al cipului = marcajul rotund / marginea tesita. Clip SOIC-8 recomandat.\n"
"In-circuit: laptop oprit, baterie si CMOS scoase; daca ID-ul iese 0x00/0xFF, cipul\n"
"  e tinut in reset de placa -> se scoate de pe placa sau se alimenteaza separat.\n"
"Cipuri >16MB (W25Q256): SET spi.4b 1. Verifica ID-ul cu ID inainte de READ.\n";

static const char W_SPI[] =
"SPI NOR 25-series generic (W25Qxx, MX25Lxx, EN25, GD25...) SOIC-8 / DIP-8, 3.3V\n"
"  pin cip  semnal    ESP32\n"
"  1        CS#   ->  {CS}\n"
"  2        DO    ->  {MISO}\n"
"  3        WP#   ->  3V3\n"
"  4        GND   ->  GND\n"
"  5        DI    ->  {MOSI}\n"
"  6        CLK   ->  {SCK}\n"
"  7        HOLD# ->  3V3\n"
"  8        VCC   ->  3V3\n"
"Cipuri de 5V (rare): NU direct - level shifter. Fire scurte (<15 cm) la 8 MHz.\n";

static const char W_I2C[] =
"EEPROM I2C 24Cxx (24C02..24C512) DIP-8 / SOIC-8, 3.3V sau 5V (cu divizor pe SDA/SCL la 5V)\n"
"  pin cip  semnal    ESP32\n"
"  1,2,3    A0,A1,A2 -> GND (adresa 0x50; A0=3V3 => 0x51 etc. -> SET i2c.addr)\n"
"  4        GND   ->  GND\n"
"  5        SDA   ->  {SDA}   (pull-up 4k7 la 3V3 daca nu exista pe placa)\n"
"  6        SCL   ->  {SCL}   (pull-up 4k7 la 3V3)\n"
"  7        WP    ->  GND (scriere permisa) / 3V3 (protejat)\n"
"  8        VCC   ->  3V3\n"
"24C02..24C16: SET i2c.abytes 1 (bitii de bloc intra in adresa). 24C32+: i2c.abytes 2.\n"
"Pagina de scriere: 24C02=8, 24C04..16=16, 24C32/64=32, 24C128/256=64, 24C512=128.\n";

static const char W_MW[] =
"EEPROM Microwire 93C46/56/66 DIP-8 (organizare x16, ORG la VCC)\n"
"  pin cip  semnal    ESP32\n"
"  1        CS    ->  {MWCS}\n"
"  2        SK    ->  {MWSK}\n"
"  3        DI    ->  {MWDI}  (ESP32 -> cip)\n"
"  4        DO    ->  {MWDO}  (cip -> ESP32)\n"
"  5        GND   ->  GND\n"
"  6        ORG   ->  3V3 (x16) - driverul lucreaza pe words\n"
"  7        NC\n"
"  8        VCC   ->  3V3\n"
"Biti adresa: 93C46=6 (64 words), 93C56=7, 93C66=8 -> SET mw.abits.\n";

static const char W_CON_TTL[] =
"Consola serial TTL 3.3V (header UART pe router/placa: OpenWrt, MikroTik RB, SBC)\n"
"  tinta          ESP32\n"
"  TX        ->   {SRX}   (tinta transmite, ESP32 asculta)\n"
"  RX        ->   {STX}   (ESP32 transmite)\n"
"  GND       ->   GND (masa comuna OBLIGATORIU)\n"
"  VCC       ->   NU se leaga (tinta are alimentarea ei)\n"
"Tinta cu UART de 5V: RX-ul ESP32 prin divizor (ex 1k/2k) sau level shifter; TX-ul\n"
"  ESP32 (3.3V) e de obicei acceptat ca HIGH. Viteza: SET ser.baud 115200 (MikroTik\n"
"  RB=115200, Cisco=9600, multe OpenWrt=115200). Apoi: SER ON, telnet ip 2324.\n";

static const char W_CON_RS232[] =
"Consola RS232 (DB9 sau RJ45 stil Cisco) - NIVELURI +/-12V: OBLIGATORIU modul MAX3232\n"
"  MAX3232 (partea TTL)     ESP32\n"
"  TXD (T1IN)          <-   {STX}\n"
"  RXD (R1OUT)         ->   {SRX}\n"
"  VCC                 <-   3V3\n"
"  GND                 <-   GND\n"
"  MAX3232 (partea RS232)   DB9 tinta (server/router)   RJ45 Cisco\n"
"  T1OUT  ->  RXD tinta    pin 2                       pin 6 (RxD)\n"
"  R1IN   <-  TXD tinta    pin 3                       pin 3 (TxD)\n"
"  GND                     pin 5                       pin 4/5 (GND)\n"
"RJ45 Cisco: 1 RTS, 2 DTR, 3 TxD, 4 GND, 5 GND, 6 RxD, 7 DSR, 8 CTS (cablul\n"
"  rollover inverseaza). DTR/RTS optional pe {SDTR}/{SRTS} prin al 2-lea canal MAX3232.\n"
"Cisco/HP: 9600 8N1; Dell iDRAC/servere: 115200; MikroTik CCR: 115200. SER ON, port 2324.\n";

static const ChipProfile PROFILES[] = {
    { "t490-mec",      "mec", 0x40000, "MEC1663 EC pe ThinkPad T490 (JTAG) - reset parola SVP", W_T490_MEC },
    { "t490-bios",     "spi", 0,       "BIOS SPI NOR pe ThinkPad T490 (SOIC-8, 3.3V)",           W_T490_BIOS },
    { "spi-soic8",     "spi", 0,       "SPI NOR 25-series generic SOIC-8/DIP-8",                 W_SPI },
    { "24c-dip8",      "i2c", 0,       "EEPROM I2C 24Cxx DIP-8/SOIC-8",                          W_I2C },
    { "93c-dip8",      "mw",  0,       "EEPROM Microwire 93C46/56/66 DIP-8",                     W_MW },
    { "console-ttl",   "ser", 0,       "consola serial TTL 3.3V (router/SBC)",                   W_CON_TTL },
    { "console-rs232", "ser", 0,       "consola RS232 DB9/RJ45 prin MAX3232 (servere, Cisco)",   W_CON_RS232 },
};
#define N_PROFILES (sizeof(PROFILES) / sizeof(PROFILES[0]))

size_t chipsCount() { return N_PROFILES; }
const ChipProfile *chipsAt(size_t i) { return i < N_PROFILES ? &PROFILES[i] : nullptr; }

const ChipProfile *chipsFind(const char *name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < N_PROFILES; i++) if (strcasecmp(PROFILES[i].name, name) == 0) return &PROFILES[i];
    return nullptr;
}

bool chipsApply(const ChipProfile *p, String &err) {
    if (!p) { err = "profil lipsa"; return false; }
    if (strcmp(p->memtype, "ser") == 0) {
        // consola: nu schimba memoria activa, doar activeaza puntea
        return cfg_set("ser.on", "1", err);
    }
    if (!cfg_set("mem.type", p->memtype, err)) return false;
    if (!cfg_set("mem.size", String(p->size).c_str(), err)) return false;
    return true;
}

static void subst(String &s, const char *tag, int8_t pin) {
    String v = (pin < 0) ? String("(-1 nefolosit)") : ("GPIO" + String((int)pin));
    s.replace(tag, v);
}

String chipsWiring(const ChipProfile *p) {
    if (!p) return String();
    String s = p->wiring;
    subst(s, "{TCK}", g_pin.tck);   subst(s, "{TMS}", g_pin.tms);   subst(s, "{TDI}", g_pin.tdi);
    subst(s, "{TDO}", g_pin.tdo);   subst(s, "{TRST}", g_pin.trst);
    subst(s, "{SCK}", g_pin.sck);   subst(s, "{MISO}", g_pin.miso); subst(s, "{MOSI}", g_pin.mosi);
    subst(s, "{CS}", g_pin.cs);
    subst(s, "{SDA}", g_pin.sda);   subst(s, "{SCL}", g_pin.scl);
    subst(s, "{MWCS}", g_pin.mwcs); subst(s, "{MWSK}", g_pin.mwsk); subst(s, "{MWDI}", g_pin.mwdi);
    subst(s, "{MWDO}", g_pin.mwdo);
    subst(s, "{STX}", g_pin.stx);   subst(s, "{SRX}", g_pin.srx);   subst(s, "{SDTR}", g_pin.sdtr);
    subst(s, "{SRTS}", g_pin.srts);
    String c = pinsConflicts(p->memtype);
    if (c.length()) s += "ATENTIE pini:\n" + c;
    return s;
}

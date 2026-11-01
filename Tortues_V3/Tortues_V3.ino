/*
****************************************************
* Tortues - Suivi d'hibernation      (version : constante VERSION ; historique : CHANGELOG.md)
****************************************************
* MATERIEL (inchange par rapport a la V2)
*   Arduino MEGA 2560
*   Ecran tactile 5" ITDB02-5.0 (800x480), UTFT + URTouch
*   RTC DS3231
*   Capteur SCD41 (temperature, humidite, CO2) en I2C
*   Lecteur carte SD en SPI, CS = broche 4
*   LED bicolore vert/rouge a anode commune (vert = 9, rouge = 10, LOW = allume)
*
* FONCTIONNEMENT
*   - Mesure toutes les MEASURE_EVERY_MIN minutes (affichage, LED, serie).
*   - Enregistrement sur carte SD toutes les RECORD_EVERY_MIN minutes,
*     aligne sur l'horloge (:00, :15, :30, :45). Valeurs brutes, pas de moyenne.
*   - Un fichier par saison : TORTaabb.BIN (ex. TORT2627.BIN pour l'hiver 2026-2027).
*   - Niveaux d'alerte : OK / SURVEILLANCE / ALERTE / URGENCE / PANNE
*     (voir seuils ci-dessous) -> LED, bandeau du haut, badge "24h".
*
* FORMAT D'UN ENREGISTREMENT (20 octets, petit-boutiste, identique a la V2)
*   jour    int16   1..31
*   mois    int16   1..12
*   annee   int16   ex. 2026 (annee complete)
*   heure   int16   0..23
*   minute  int16   0..59
*   temp    float32 degres C, arrondi a 0.1 (peut etre negatif)
*   hum     int16   % HR, arrondi a l'entier
*   co2     int16   ppm
*   flag    int16   bit0 = temperature hors plage OK, bit1 = humidite, bit2 = CO2
*
* CALIBRATION DE L'OFFSET DE TEMPERATURE : voir README.md dans ce dossier.
*/

#include <URTouch.h>
#include <DFRobot_SCD4X.h>
#include <UTFT.h>
#include <SdFat.h>
#include <RTClib.h>
#include <Wire.h>
#include <SPI.h>
#include <avr/wdt.h>

const char VERSION[] = "V3.11";             // affiche en bas a droite de l'ecran et au demarrage

// =====================================================================
//  REGLAGES UTILISATEUR
// =====================================================================
const float   TEMP_OFFSET       = 2.3;   // compensation capteur (degres C) - voir README.md
const uint8_t MEASURE_EVERY_MIN = 1;     // mesure (ecran + LED) toutes les N minutes
const uint8_t RECORD_EVERY_MIN  = 15;    // enregistrement SD toutes les N minutes (multiple de MEASURE_EVERY_MIN)
const uint8_t RESTART_AFTER_FAULTS = 10; // mesures capteur ratees d'affilee avant redemarrage complet (10 min)
const uint8_t CONFIRM_COUNT     = 3;     // mesures consecutives avant de changer de niveau d'alerte (3 min)

// Dalle tactile : valeurs brutes (0-4095) lues aux bords de l'ecran - voir README.md "Boutons tactiles".
// Le programme fait lui-meme la conversion en pixels 800x480 : le fichier URTouchCD.h de la
// bibliotheque (calibre pour un ecran 320x240) n'intervient plus.
const int16_t TOUCH_RAW_LEFT  = 3490, TOUCH_RAW_RIGHT  = 327;    // bord gauche -> bord droit (axe inverse)
const int16_t TOUCH_RAW_TOP   = 494,  TOUCH_RAW_BOTTOM = 3737;   // bord haut -> bord bas
const bool    TOUCH_SWAP_XY   = true;    // canal B = horizontale (decroissante), canal A = verticale (croissante)

// Seuils. S = surveillance, A = alerte, U = urgence. Plage OK = entre LOW_S et HIGH_S.
const float T_LOW_U = 3.0, T_LOW_A = 4.0, T_LOW_S = 4.5, T_HIGH_S = 8.0, T_HIGH_A = 9.0, T_HIGH_U = 10.0;
const float T_MARGIN = 0.3;                               // marge pour redescendre d'un niveau
const float H_LOW_A = 55, H_LOW_S = 65, H_HIGH_S = 92, H_HIGH_A = 95;   // pas de niveau urgence
const float H_MARGIN = 2;
const float C_HIGH_S = 1000, C_HIGH_A = 2000, C_HIGH_U = 3000;
const float C_MARGIN = 200;

// =====================================================================
//  MATERIEL
// =====================================================================
const uint8_t LED_GREEN = 9;
const uint8_t LED_RED   = 10;
const uint8_t SD_CS     = 4;

DFRobot_SCD4X scd(&Wire, SCD4X_I2C_ADDR);
RTC_DS3231    rtc;
SdFat         sd;
File          file;
UTFT          tft(ITDB50, 38, 39, 40, 41);
URTouch       touch(6, 5, 4, 3, 2);

extern uint8_t SmallFont[];
extern uint8_t BigFont[];
extern uint8_t Mono16x32[];
extern uint8_t Mono32x64[];

// =====================================================================
//  DONNEES
// =====================================================================
struct __attribute__((packed)) Record {
  int16_t day, month, year, hour, minute;
  float   temp;
  int16_t hum, co2, flag;
};
const uint8_t REC_SIZE = sizeof(Record);   // = 20

enum Level : uint8_t { LVL_OK = 0, LVL_WATCH = 1, LVL_ALERT = 2, LVL_URGENT = 3, LVL_FAULT = 4 };

struct Metric {          // niveau confirme (avec hysteresis et confirmation)
  uint8_t level, pending, count;
};
Metric mTemp = {0, 0, 0}, mHum = {0, 0, 0}, mCo2 = {0, 0, 0};
bool levelsInit = false;      // niveaux initialises par la premiere mesure valide
uint8_t globalLevel = LVL_OK;

Record  last;                 // derniere mesure affichee
bool    lastValid = false;    // la derniere mesure est-elle exploitable ?
bool    sensorFault = false;
uint8_t sensorFailCount = 0;  // mesures ratees consecutives
bool    sdFault     = false;
bool    rtcLost     = false;
uint8_t sdFaultCode = 0;      // 1 = ouverture, 2 = ecriture, 3 = lecture

char     fileName[13] = "TORTLOST.BIN";
char     csvName[13]  = "TORTLOST.CSV";   // copie lisible, une ligne par enregistrement
char     logName[13]  = "TORTLOST.LOG";   // journal des evenements (pannes, niveaux, demarrages)
char     sdFaultSince[6] = "";            // heure de debut de la panne SD en cours
uint8_t  sensorFaultMinutes = 0;          // duree de la panne capteur en cours
uint8_t  prevGlobalLevel = LVL_OK;
uint8_t  resetCause = 0;                  // registre MCUSR lu au demarrage
uint32_t fileBytes = 0;       // taille du fichier
uint32_t numRec    = 0;       // nombre d'enregistrements
uint32_t viewTop   = 0;       // offset de l'enregistrement affiche en haut de l'historique
bool     followLive = true;   // l'historique suit-il le dernier enregistrement ?
bool     buttonsActive = false;   // boutons dessines en blanc (actifs) ou gris

// Fenetre glissante 24 h des niveaux (une entree par mesure)
const uint16_t RING_SIZE = (24UL * 60) / MEASURE_EVERY_MIN;   // 480 pour 3 min
uint8_t  ring[RING_SIZE];
uint16_t ringPos = 0;
uint16_t ringCount = 0;

int16_t lastMinuteKey = -1;   // heure*60+minute de la derniere mesure declenchee
int8_t  lastClockMinute = -1;
unsigned long lastRtcPoll = 0;

// =====================================================================
//  DISPOSITION ECRAN (800 x 480)
// =====================================================================
const int BANNER_Y1 = 3,  BANNER_Y2 = 73;
const int HEADER_Y  = 80;
const int ROW_Y0    = 106;
const int ROW_STEP  = 34;
const uint8_t ROWS  = 10;
const int STATUS_Y  = 458;
const int COL_DATE = 10, COL_TIME = 130, COL_TEMP = 270, COL_HUM = 400, COL_CO2 = 520;
const int LEFT_X1 = 3, LEFT_X2 = 659;

// =====================================================================
//  OUTILS
// =====================================================================
void two(char *buf, int v) {               // 2 chiffres avec zero devant
  buf[0] = '0' + (v / 10) % 10; buf[1] = '0' + v % 10; buf[2] = 0;
}
void fmtDate(char *buf, const Record &r) { // "25/01"
  two(buf, r.day); buf[2] = '/'; two(buf + 3, r.month);
}
void fmtTime(char *buf, int h, int m) {    // "14:30"
  two(buf, h); buf[2] = ':'; two(buf + 3, m);
}
void fmtTemp(char *buf, float t)  { dtostrf(t, 4, 1, buf); }        // " 5.4" / "-2.5" / "10.5"
void fmtHum(char *buf, int h)     { snprintf(buf, 4, "%3d", h); }
void fmtCo2(char *buf, int c)     { snprintf(buf, 5, "%4d", c); }

void levelColor(uint8_t lvl, uint8_t &r, uint8_t &g, uint8_t &b, bool &darkText) {
  darkText = false;
  switch (lvl) {
    case LVL_OK:     r = 0;   g = 150; b = 0;   break;
    case LVL_WATCH:  r = 255; g = 220; b = 0;   darkText = true; break;
    case LVL_ALERT:  r = 255; g = 120; b = 0;   darkText = true; break;
    case LVL_URGENT: r = 220; g = 0;   b = 0;   break;
    default:         r = 110; g = 110; b = 110; break;
  }
}
void useLevelColors(uint8_t lvl) {         // fond = couleur du niveau, texte contraste
  uint8_t r, g, b; bool dark;
  levelColor(lvl, r, g, b, dark);
  tft.setBackColor(r, g, b);
  if (dark) tft.setColor(0, 0, 0); else tft.setColor(255, 255, 255);
}
void useNormalColors() { tft.setColor(255, 255, 255); tft.setBackColor(0, 0, 0); }

// =====================================================================
//  NIVEAUX D'ALERTE
// =====================================================================
uint8_t lvlHigh(float v, float s, float a, float u, bool hasU) {
  if (hasU && v > u) return LVL_URGENT;
  if (v > a) return LVL_ALERT;
  if (v > s) return LVL_WATCH;
  return LVL_OK;
}
uint8_t lvlLow(float v, float s, float a, float u, bool hasU) {
  if (hasU && v < u) return LVL_URGENT;
  if (v < a) return LVL_ALERT;
  if (v < s) return LVL_WATCH;
  return LVL_OK;
}
// Niveau brut (sans hysteresis) de chaque grandeur
uint8_t rawTemp(float t) { return max(lvlLow(t, T_LOW_S, T_LOW_A, T_LOW_U, true), lvlHigh(t, T_HIGH_S, T_HIGH_A, T_HIGH_U, true)); }
uint8_t rawHum(float h)  { return max(lvlLow(h, H_LOW_S, H_LOW_A, 0, false),      lvlHigh(h, H_HIGH_S, H_HIGH_A, 0, false)); }
uint8_t rawCo2(float c)  { return lvlHigh(c, C_HIGH_S, C_HIGH_A, C_HIGH_U, true); }

// Niveau avec marge : pour redescendre, la valeur doit etre revenue d'au moins "margin" dans la plage inferieure
uint8_t withMargin(uint8_t raw, uint8_t cur, uint8_t rawWithMargin) {
  if (raw >= cur) return raw;
  return min(cur, rawWithMargin);
}
uint8_t hystTemp(float t, uint8_t cur) {
  uint8_t m = max(lvlLow(t, T_LOW_S + T_MARGIN, T_LOW_A + T_MARGIN, T_LOW_U + T_MARGIN, true),
                  lvlHigh(t, T_HIGH_S - T_MARGIN, T_HIGH_A - T_MARGIN, T_HIGH_U - T_MARGIN, true));
  return withMargin(rawTemp(t), cur, m);
}
uint8_t hystHum(float h, uint8_t cur) {
  uint8_t m = max(lvlLow(h, H_LOW_S + H_MARGIN, H_LOW_A + H_MARGIN, 0, false),
                  lvlHigh(h, H_HIGH_S - H_MARGIN, H_HIGH_A - H_MARGIN, 0, false));
  return withMargin(rawHum(h), cur, m);
}
uint8_t hystCo2(float c, uint8_t cur) {
  uint8_t m = lvlHigh(c, C_HIGH_S - C_MARGIN, C_HIGH_A - C_MARGIN, C_HIGH_U - C_MARGIN, true);
  return withMargin(rawCo2(c), cur, m);
}
// Confirmation : il faut CONFIRM_COUNT mesures consecutives au nouveau niveau
void updateMetric(Metric &m, uint8_t candidate) {
  if (candidate == m.level) { m.pending = candidate; m.count = 0; return; }
  if (candidate == m.pending) m.count++; else { m.pending = candidate; m.count = 1; }
  if (m.count >= CONFIRM_COUNT) { m.level = candidate; m.count = 0; }
}
void computeGlobalLevel() {
  if (sensorFault || sdFault || rtcLost) { globalLevel = LVL_FAULT; return; }
  globalLevel = max(mTemp.level, max(mHum.level, mCo2.level));
}
void pushRing(uint8_t lvl) {
  ring[ringPos] = lvl;
  ringPos = (ringPos + 1) % RING_SIZE;
  if (ringCount < RING_SIZE) ringCount++;
}
uint8_t worst24h() {                        // pannes exclues (niveaux 0..3)
  uint8_t w = 0;
  for (uint16_t i = 0; i < ringCount; i++) if (ring[i] <= LVL_URGENT && ring[i] > w) w = ring[i];
  return w;
}
void clearRing() { ringCount = 0; ringPos = 0; }

// =====================================================================
//  LED
// =====================================================================
void updateLed() {
  unsigned long t = millis();
  bool g = false, r = false;
  switch (globalLevel) {
    case LVL_OK:     g = true; break;
    case LVL_WATCH:  g = ((t / 1000) % 2) == 0; break;      // vert clignotant lent
    case LVL_ALERT:  r = true; break;
    case LVL_URGENT: r = ((t / 200) % 2) == 0; break;       // rouge clignotant rapide
    default: { bool a = ((t / 500) % 2) == 0; r = a; g = !a; }   // panne : alternance
  }
  digitalWrite(LED_GREEN, g ? LOW : HIGH);
  digitalWrite(LED_RED,   r ? LOW : HIGH);
}

// =====================================================================
//  CARTE SD
// =====================================================================
void buildFileName(const DateTime &now) {
  if (rtcLost || now.year() < 2020) { strcpy(fileName, "TORTLOST.BIN"); strcpy(csvName, "TORTLOST.CSV"); strcpy(logName, "TORTLOST.LOG"); return; }
  int season = (now.month() >= 7) ? now.year() : now.year() - 1;
  snprintf(fileName, sizeof(fileName), "TORT%02d%02d.BIN", season % 100, (season + 1) % 100);
  snprintf(csvName,  sizeof(csvName),  "TORT%02d%02d.CSV", season % 100, (season + 1) % 100);
  snprintf(logName,  sizeof(logName),  "TORT%02d%02d.LOG", season % 100, (season + 1) % 100);
}
// Journal des evenements : "date;heure;type;detail". Echec silencieux si la carte ne repond pas.
void logEvent(const __FlashStringHelper *type, const char *detail) {
  DateTime now = rtc.now();
  char stamp[20];
  snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d;%02d:%02d;", now.year(), now.month(), now.day(), now.hour(), now.minute());
  Serial.print(F("LOG ")); Serial.print(stamp); Serial.print(type); Serial.print(';'); Serial.println(detail);
  File lg;
  if (!lg.open(logName, O_WRONLY | O_CREAT | O_APPEND)) return;
  lg.print(stamp); lg.print(type); lg.print(';'); lg.println(detail);
  lg.sync();
  lg.close();
}
const char *levelName(uint8_t lvl) {
  switch (lvl) {
    case LVL_OK: return "OK"; case LVL_WATCH: return "SURVEILLANCE"; case LVL_ALERT: return "ALERTE";
    case LVL_URGENT: return "URGENCE"; default: return "PANNE";
  }
}
// Ajoute la ligne CSV correspondante (fichier texte a cote du binaire). Separateur ';', decimale '.'
bool writeCsv(const Record &rec) {
  File csv;
  if (!csv.open(csvName, O_WRONLY | O_CREAT | O_APPEND)) return false;
  bool ok = true;
  if (csv.fileSize() == 0) ok = csv.println(F("date;heure;temp;hum;co2;flag")) > 0;
  char line[40], t[8];
  dtostrf(rec.temp, 1, 1, t);
  snprintf(line, sizeof(line), "%04d-%02d-%02d;%02d:%02d;%s;%d;%d;%d",
           rec.year, rec.month, rec.day, rec.hour, rec.minute, t, rec.hum, rec.co2, rec.flag);
  ok = (csv.println(line) > 0) && ok;
  ok = csv.sync() && ok;
  csv.close();
  return ok;
}
// Ouvre le fichier en ajout, repare une fin de fichier incomplete, met a jour taille et compteur
bool openForAppend() {
  if (!file.open(fileName, O_WRONLY | O_CREAT | O_APPEND)) {
    // carte peut-etre retiree/remise : nouvelle initialisation puis nouvel essai
    if (!sd.begin(SD_CS, SPI_HALF_SPEED)) return false;
    if (!file.open(fileName, O_WRONLY | O_CREAT | O_APPEND)) return false;
  }
  uint32_t size = file.fileSize();
  if (size % REC_SIZE != 0) {                 // ecriture interrompue (coupure de courant)
    file.truncate(size - size % REC_SIZE);
    char d[32]; snprintf(d, sizeof(d), "fichier tronque de %u octets", (unsigned)(size % REC_SIZE));
    logEvent(F("SD"), d);
  }
  fileBytes = file.fileSize();
  numRec = fileBytes / REC_SIZE;
  return true;
}
bool writeRecord(const Record &rec) {
  if (!openForAppend()) { sdFaultCode = 1; return false; }
  bool ok = (file.write(&rec, REC_SIZE) == REC_SIZE);
  ok = file.sync() && ok;
  if (ok) { fileBytes = file.fileSize(); numRec = fileBytes / REC_SIZE; }
  file.close();
  if (ok) ok = writeCsv(rec);
  if (!ok) sdFaultCode = 2;
  return ok;
}
bool readRecord(uint32_t pos, Record &rec) {   // fichier deja ouvert en lecture
  return file.seekSet(pos) && (file.read(&rec, REC_SIZE) == REC_SIZE);
}

// =====================================================================
//  CAPTEUR
// =====================================================================
void sensorStart() {
  scd.enablePeriodMeasure(SCD4X_STOP_PERIODIC_MEASURE);
  delay(500);
  scd.setTempComp(TEMP_OFFSET);
  scd.enablePeriodMeasure(SCD4X_START_PERIODIC_MEASURE);
}
// Attend une mesure prete (max 12 s) et la lit. false si rien d'exploitable.
bool readSensor(float &t, int &h, int &c) {
  unsigned long start = millis();
  while (!scd.getDataReadyStatus()) {
    if (millis() - start > 12000UL) return false;
    delay(250);
    wdt_reset();
    updateLed();
  }
  DFRobot_SCD4X::sSensorMeasurement_t d;
  scd.readMeasurement(&d);
  if (d.CO2ppm == 0 || d.humidity <= 0.0 || d.CO2ppm > 40000) return false;   // trame nulle / aberrante
  t = round(d.temp * 10.0) / 10.0;
  h = (int)(d.humidity + 0.5);
  c = d.CO2ppm;
  return true;
}

// =====================================================================
//  AFFICHAGE
// =====================================================================
void drawFrame() {
  tft.clrScr();
  useNormalColors();
  tft.drawRect(0, 0, 662, 479);
  tft.drawLine(2, 2, 660, 2); tft.drawLine(2, 2, 2, 477); tft.drawLine(660, 2, 660, 477);
  // separations
  tft.drawLine(2, 76, 660, 76);
  tft.drawLine(2, 100, 660, 100);
  for (uint8_t i = 0; i <= ROWS; i++) tft.drawLine(2, ROW_Y0 - 2 + i * ROW_STEP, 660, ROW_Y0 - 2 + i * ROW_STEP);
  tft.drawLine(2, 450, 660, 450);
  // en-tetes
  tft.setFont(BigFont);
  tft.print("DATE",  COL_DATE, HEADER_Y);
  tft.print("HEURE", COL_TIME, HEADER_Y);
  tft.print("TEMP",  COL_TEMP, HEADER_Y);
  tft.print("HUM",   COL_HUM,  HEADER_Y);
  tft.print("CO2",   COL_CO2,  HEADER_Y);
  // colonne de droite : horloge (neutre), badge 24h, boutons
  tft.setColor(255, 255, 255);
  tft.drawRoundRect(670, 3, 793, 35);
  useNormalColors();
}

void drawClock(const DateTime &now) {
  char buf[6]; fmtTime(buf, now.hour(), now.minute());
  tft.setFont(BigFont);
  useNormalColors();
  tft.print(buf, 692, 12);
}

void drawBadge24h() {
  uint8_t lvl = worst24h();
  uint8_t r, g, b; bool dark; levelColor(lvl, r, g, b, dark);
  tft.setColor(r, g, b);
  tft.fillRoundRect(670, 42, 793, 72);
  useLevelColors(lvl);
  tft.setFont(BigFont);
  tft.print("24h", 708, 49);
  useNormalColors();
}

const uint8_t GREY = 90;
// Dessine les trois boutons (fleches, cercle, libelles). available=false -> tout en gris
void drawButtons(bool available) {
  uint8_t c = available ? 255 : GREY;
  tft.setColor(0, 0, 0);
  tft.fillRect(664, 78, 799, 290);          // zone UP + DOWN
  tft.fillRect(664, 370, 799, 460);         // zone NOW (retour a la page 1)
  tft.setColor(c, c, c); tft.setBackColor(0, 0, 0);
  tft.setFont(BigFont);
  tft.print("UP", 710, 80);
  for (int ii = 0, jj = 100; jj <= 150; jj++, ii++) tft.drawLine(724 - ii, jj + 1, 724 + ii, jj + 1);
  tft.print("DOWN", 690, 260);
  for (int ii = 0, jj = 200; jj <= 250; jj++, ii++) tft.drawLine(675 + ii, jj + 1, 773 - ii, jj + 1);
  tft.fillCircle(725, 400, 25);
  tft.print("NOW", 707, 440);
  useNormalColors();
}
// which 0=UP 1=DOWN 2=NOW ; state 0=normal 1=appuye (inverse) 2=butee (gris)
void drawButtonState(uint8_t which, uint8_t state) {
  tft.setFont(BigFont);
  if (state == 1)      { tft.setColor(0, 0, 0); tft.setBackColor(255, 255, 255); }
  else if (state == 2) { tft.setColor(GREY, GREY, GREY); tft.setBackColor(0, 0, 0); }
  else                 { tft.setColor(255, 255, 255); tft.setBackColor(0, 0, 0); }
  if (which == 0) tft.print("UP", 710, 80);
  else if (which == 1) tft.print("DOWN", 690, 260);
  else tft.print("NOW", 707, 440);
  useNormalColors();
}

void drawScrollIndicator() {              // "page / total" sous la fleche DOWN (une ligne vide d'ecart)
  tft.setColor(0, 0, 0);
  tft.fillRect(664, 294, 799, 350);
  useNormalColors();
  if (numRec == 0) return;
  char a[12], b[12], line[20];
  // Pages comptees depuis le plus recent (page 1) ; la page la plus ancienne, calee, compte comme une page entiere.
  const uint32_t page = (uint32_t)ROWS * REC_SIZE;
  const uint32_t last = fileBytes - REC_SIZE;
  uint32_t pageNo = (last > viewTop) ? (last - viewTop + page - 1) / page + 1 : 1;
  uint32_t pages  = (numRec + ROWS - 1) / ROWS;
  snprintf(a, sizeof(a), "%lu", (unsigned long)pageNo);
  snprintf(b, sizeof(b), "%lu", (unsigned long)pages);
  snprintf(line, sizeof(line), "%s/%s", a, b);
  uint8_t len = strlen(line);
  int labelY;
  tft.setFont(BigFont);
  if (len <= 8) {                         // tient sur une ligne, aligne a droite
    tft.print(line, 796 - len * 16, 300);
    labelY = 322;
  } else {                                // deux lignes, alignees a droite
    tft.print(a, 796 - strlen(a) * 16, 296);
    snprintf(line, sizeof(line), "/%s", b);
    tft.print(line, 796 - strlen(line) * 16, 314);
    labelY = 336;
  }
  tft.setFont(SmallFont);
  tft.print("page / total", 692, labelY);
  tft.setFont(BigFont);
}

// Bandeau du haut : derniere mesure, fond = niveau global
void drawBanner() {
  uint8_t lvl = globalLevel;
  uint8_t r, g, b; bool dark; levelColor(lvl, r, g, b, dark);
  tft.setColor(r, g, b);
  tft.fillRect(LEFT_X1, BANNER_Y1, LEFT_X2, BANNER_Y2);
  useLevelColors(lvl);
  char buf[8];
  tft.setFont(Mono16x32);
  fmtDate(buf, last);                     tft.print(buf, 8, 6);
  fmtTime(buf, last.hour, last.minute);   tft.print(buf, 8, 38);
  // unites
  tft.print("C",   256, 36);
  tft.print("%",   404, 36);
  tft.print("ppm", 580, 36);
  tft.drawCircle(262, 14, 3);             // symbole degre
  tft.setFont(Mono32x64);
  if (lastValid) {
    fmtTemp(buf, last.temp); tft.print(buf, 124, 6);
    fmtHum(buf, last.hum);   tft.print(buf, 304, 6);
    fmtCo2(buf, last.co2);   tft.print(buf, 444, 6);
  } else {
    tft.print("--.-", 124, 6);
    tft.print("---",  304, 6);
    tft.print("----", 444, 6);
  }
  useNormalColors();
}

// Ligne d'etat en bas
void drawStatus() {
  tft.setFont(BigFont);
  tft.setColor(0, 0, 0);
  tft.fillRect(LEFT_X1, STATUS_Y - 4, LEFT_X2, 477);
  char buf[40];
  if (sensorFault) {
    tft.setColor(255, 255, 255); tft.setBackColor(220, 0, 0);
    tft.print("PANNE CAPTEUR : pas de mesure", 8, STATUS_Y);
  } else if (sdFault) {
    tft.setColor(255, 255, 255); tft.setBackColor(220, 0, 0);
    if (sdFaultCode == 1)      tft.print("PANNE CARTE SD : ouverture", 8, STATUS_Y);
    else if (sdFaultCode == 2) tft.print("PANNE CARTE SD : ecriture", 8, STATUS_Y);
    else                       tft.print("PANNE CARTE SD : lecture", 8, STATUS_Y);
  } else if (rtcLost) {
    tft.setColor(0, 0, 0); tft.setBackColor(255, 220, 0);
    tft.print("HORLOGE PERDUE : regler l'heure", 8, STATUS_Y);
  } else {
    useNormalColors();
    snprintf(buf, sizeof(buf), "OK  %lu enreg.  %s", (unsigned long)numRec, fileName);
    tft.print(buf, 8, STATUS_Y);
  }
  useNormalColors();
  tft.print((char*)VERSION, LEFT_X2 - 4 - strlen(VERSION) * 16, STATUS_Y);   // version a droite
}

void clearRow(uint8_t row) {
  int y = ROW_Y0 + row * ROW_STEP;
  tft.setColor(0, 0, 0);
  tft.fillRect(LEFT_X1, y, LEFT_X2, y + 31);
  useNormalColors();
}
void printCell(const char *txt, int x, int y, uint8_t lvl) {
  if (lvl == LVL_OK) useNormalColors(); else useLevelColors(lvl);
  tft.print(txt, x, y);
  useNormalColors();
}
void drawRow(uint8_t row, const Record &r) {
  clearRow(row);
  int y = ROW_Y0 + row * ROW_STEP;
  char buf[8];
  tft.setFont(Mono16x32);
  fmtDate(buf, r);                 tft.print(buf, COL_DATE, y);
  fmtTime(buf, r.hour, r.minute);  tft.print(buf, COL_TIME, y);
  fmtTemp(buf, r.temp);            printCell(buf, COL_TEMP, y, rawTemp(r.temp));
  fmtHum(buf, r.hum);              printCell(buf, COL_HUM,  y, rawHum(r.hum));
  fmtCo2(buf, r.co2);              printCell(buf, COL_CO2,  y, rawCo2(r.co2));
}

// Historique : ROWS lignes a partir de viewTop, du plus recent (haut) au plus ancien (bas)
void drawHistory() {
  if (numRec == 0) { for (uint8_t i = 0; i < ROWS; i++) clearRow(i); return; }
  if (!file.open(fileName, O_RDONLY)) { sdFault = true; sdFaultCode = 3; return; }
  if (viewTop > fileBytes - REC_SIZE) viewTop = fileBytes - REC_SIZE;
  uint32_t pos = viewTop;
  bool more = true;
  Record r;
  for (uint8_t row = 0; row < ROWS; row++) {
    if (more && readRecord(pos, r)) drawRow(row, r); else clearRow(row);
    if (pos < REC_SIZE) more = false; else pos -= REC_SIZE;
  }
  file.close();
}

// =====================================================================
//  TACTILE
// =====================================================================
void flashLimit(uint8_t which) {            // butee : bouton en rouge 300 ms
  drawButtonState(which, 2);
  unsigned long t0 = millis();
  while (millis() - t0 < 300) { updateLed(); wdt_reset(); }
  drawButtonState(which, 1);
}
// dir +1 : vers le plus recent (UP) ; -1 : vers le plus ancien (DOWN). false si en butee.
// Defilement par page : un appui deplace l'historique de ROWS enregistrements (une page), la premiere
// ou la derniere page restant pleine. dir +1 : vers le plus recent (UP) ; -1 : vers le plus ancien (DOWN).
// false si deja en butee.
bool scrollBy(int8_t dir) {
  if (numRec < 2) return false;
  const uint32_t page = (uint32_t)ROWS * REC_SIZE;
  const uint32_t last = fileBytes - REC_SIZE;                 // offset du plus recent
  const uint32_t first = (numRec > ROWS) ? (uint32_t)(ROWS - 1) * REC_SIZE : last;   // page la plus ancienne pleine
  if (dir > 0) {
    if (viewTop >= last) return false;
    viewTop = (last - viewTop > page) ? viewTop + page : last;
    if (viewTop == last) followLive = true;
  } else {
    if (viewTop <= first) return false;
    viewTop = (viewTop - first > page) ? viewTop - page : first;
    followLive = false;
  }
  drawHistory();
  drawScrollIndicator();
  if (sdFault) { computeGlobalLevel(); drawStatus(); drawBanner(); }
  return true;
}
void scrollAction(uint8_t which, int8_t dir) {
  drawButtonState(which, 1);
  unsigned long lastStep = 0;
  uint16_t repeatDelay = 700;               // maintien = repetition (une page toutes les 400 ms)
  do {
    if (millis() - lastStep >= repeatDelay) {
      lastStep = millis(); repeatDelay = 400;
      if (!scrollBy(dir)) flashLimit(which);
    }
    updateLed(); wdt_reset();
  } while (touch.dataAvailable());
  drawButtonState(which, 0);
}
void refreshAction() {                      // bouton NOW : page 1 (la plus recente) + remise a zero du badge 24h
  drawButtonState(2, 1);
  followLive = true;
  if (numRec > 0) viewTop = fileBytes - REC_SIZE;
  clearRing();                              // remise a zero du badge 24h
  drawHistory(); drawScrollIndicator(); drawBadge24h();
  while (touch.dataAvailable()) { updateLed(); wdt_reset(); }
  drawButtonState(2, 0);
}
// Lecture brute de la dalle (8 echantillons, sans le min et le max) puis conversion en pixels.
// false si l'appui est trop bref ou les valeurs incoherentes.
bool touchRead(int16_t &x, int16_t &y) {
  uint32_t sa = 0, sb = 0;
  uint16_t minA = 4095, maxA = 0, minB = 4095, maxB = 0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (!touch.dataAvailable()) break;
    touch.calibrateRead();                  // valeurs brutes, sans calibration de la bibliotheque
    uint16_t a = (uint16_t)touch.TP_Y;      // canal "X" du controleur
    uint16_t b = (uint16_t)touch.TP_X;      // canal "Y" du controleur
    if (a == 0 || a >= 4095 || b == 0 || b >= 4095) continue;
    sa += a; sb += b; n++;
    if (a < minA) minA = a; if (a > maxA) maxA = a;
    if (b < minB) minB = b; if (b > maxB) maxB = b;
  }
  if (n < 4) return false;
  sa -= minA + maxA; sb -= minB + maxB; n -= 2;
  int32_t ra = sa / n, rb = sb / n;
  if (TOUCH_SWAP_XY) { int32_t t = ra; ra = rb; rb = t; }
  x = constrain(map(ra, TOUCH_RAW_LEFT, TOUCH_RAW_RIGHT, 0, 799), 0, 799);
  y = constrain(map(rb, TOUCH_RAW_TOP, TOUCH_RAW_BOTTOM, 0, 479), 0, 479);
  return true;
}
// Zones : toute la colonne de droite (x >= 664) coupee en trois bandes a mi-distance entre les boutons
// (UP y=125, DOWN y=225, NOW y=400), pour tolerer une calibration verticale approximative.
void handleTouch() {
  if (!touch.dataAvailable()) return;
  int16_t x, y;
  if (touchRead(x, y) && x >= 664) {
    if (y <= 175)      scrollAction(0, +1);
    else if (y <= 312) scrollAction(1, -1);
    else               refreshAction();
  }
  while (touch.dataAvailable()) { updateLed(); wdt_reset(); }
}

// =====================================================================
//  MESURE
// =====================================================================
void serialCsv(const Record &r, bool valid, bool recorded) {
  char d[6], t[6];
  fmtDate(d, r); fmtTime(t, r.hour, r.minute);
  Serial.print(r.year); Serial.print('-'); Serial.print(d[3]); Serial.print(d[4]); Serial.print('-'); Serial.print(d[0]); Serial.print(d[1]);
  Serial.print(' '); Serial.print(t); Serial.print(';');
  if (valid) { Serial.print(r.temp, 1); Serial.print(';'); Serial.print(r.hum); Serial.print(';'); Serial.print(r.co2); }
  else Serial.print(F(";;"));
  Serial.print(';'); Serial.print(r.flag);
  Serial.print(';'); Serial.print(globalLevel);
  Serial.print(';'); Serial.println(recorded ? 1 : 0);
}

void doMeasurement(const DateTime &now, bool record) {
  Record rec;
  rec.day = now.day(); rec.month = now.month(); rec.year = now.year();
  rec.hour = now.hour(); rec.minute = now.minute();
  rec.temp = 0; rec.hum = 0; rec.co2 = 0; rec.flag = 0;

  float t; int h, c;
  bool wasSensorFault = sensorFault, wasSdFault = sdFault;
  bool ok = readSensor(t, h, c);
  if (ok) {
    sensorFault = false;
    rec.temp = t; rec.hum = h; rec.co2 = c;
    if (rawTemp(t) > LVL_OK) rec.flag |= 1;
    if (rawHum(h)  > LVL_OK) rec.flag |= 2;
    if (rawCo2(c)  > LVL_OK) rec.flag |= 4;
    if (!levelsInit) {                      // premiere mesure valide : niveau immediat, sans confirmation
      mTemp.level = rawTemp(t); mHum.level = rawHum(h); mCo2.level = rawCo2(c);
      levelsInit = true;
    } else {
      updateMetric(mTemp, hystTemp(t, mTemp.level));
      updateMetric(mHum,  hystHum(h, mHum.level));
      updateMetric(mCo2,  hystCo2(c, mCo2.level));
    }
  } else {
    sensorFault = true;
    if (sensorFailCount < 255) sensorFailCount++;
    Serial.println(F("CAPTEUR: pas de donnee, reinitialisation"));
    if (sensorFailCount >= RESTART_AFTER_FAULTS) {
      // panne persistante : redemarrage complet (bus I2C, capteur, ecran) via le chien de garde
      logEvent(F("REDEMARRAGE"), "capteur muet depuis 10 min");
      Serial.flush();
      wdt_enable(WDTO_15MS);
      while (1) {}
    }
    if (scd.begin()) sensorStart();         // tentative de relance
  }
  if (ok) sensorFailCount = 0;
  if (sensorFault && !wasSensorFault) { sensorFaultMinutes = 0; logEvent(F("CAPTEUR"), "pas de donnee"); }
  if (sensorFault && sensorFaultMinutes < 255) sensorFaultMinutes += MEASURE_EVERY_MIN;
  if (!sensorFault && wasSensorFault) {
    char d[32]; snprintf(d, sizeof(d), "retour OK apres %u min", (unsigned)sensorFaultMinutes);
    logEvent(F("CAPTEUR"), d);
  }
  last = rec; lastValid = ok;

  bool recorded = false;
  if (ok && record) {
    buildFileName(now);
    if (writeRecord(rec)) {
      sdFault = false; recorded = true;
      if (followLive) viewTop = fileBytes - REC_SIZE;
    } else {
      sdFault = true;
    }
  } else {
    // pas d'ecriture a cette mesure : on verifie quand meme que la carte repond (detection en 3 min)
    bool present = file.open(fileName, O_RDONLY);
    if (present) file.close();
    else if (sd.begin(SD_CS, SPI_HALF_SPEED) && file.open(fileName, O_RDONLY)) { present = true; file.close(); }
    if (!present) { sdFault = true; sdFaultCode = 1; }
    else if (sdFault && sdFaultCode == 1) sdFault = false;   // carte remise
  }
  if (recorded || sdFault) { drawHistory(); drawScrollIndicator(); }   // peut detecter une panne de lecture
  // transitions de la carte SD (le debut n'est souvent pas inscriptible : on note l'heure, on l'ecrit au retour)
  if (sdFault && !wasSdFault) {
    fmtTime(sdFaultSince, now.hour(), now.minute());
    logEvent(F("SD"), sdFaultCode == 1 ? "ouverture impossible" : sdFaultCode == 2 ? "ecriture impossible" : "lecture impossible");
  }
  if (!sdFault && wasSdFault) {
    char d[32]; snprintf(d, sizeof(d), "retour OK (panne depuis %s)", sdFaultSince);
    logEvent(F("SD"), d);
  }
  computeGlobalLevel();
  if (globalLevel != prevGlobalLevel) {
    char d[48], tb[8];
    if (lastValid) { dtostrf(t, 1, 1, tb); snprintf(d, sizeof(d), "%s -> %s;temp=%s;hum=%d;co2=%d", levelName(prevGlobalLevel), levelName(globalLevel), tb, h, c); }
    else snprintf(d, sizeof(d), "%s -> %s", levelName(prevGlobalLevel), levelName(globalLevel));
    logEvent(F("NIVEAU"), d);
    prevGlobalLevel = globalLevel;
  }
  pushRing(globalLevel);
  updateLed();

  drawBanner();
  drawBadge24h();
  drawStatus();
  if ((numRec >= 2) != buttonsActive) { buttonsActive = (numRec >= 2); drawButtons(buttonsActive); }
  serialCsv(rec, ok, recorded);
}

// =====================================================================
//  DEMARRAGE
// =====================================================================
void bootMsg(const char *msg, int line, bool error) {
  tft.setFont(BigFont);
  tft.setColor(0, 0, 0);
  tft.fillRect(0, line, 799, line + 16);          // efface la ligne
  if (error) { tft.setColor(255, 255, 255); tft.setBackColor(200, 0, 0); }
  else       { tft.setColor(255, 255, 255); tft.setBackColor(0, 0, 0); }
  tft.print(msg, 20, line);
  useNormalColors();
  Serial.println(msg);
}

void setup() {
  resetCause = MCUSR; MCUSR = 0; wdt_disable();   // cause du demarrage, puis securite apres un chien de garde
  pinMode(LED_GREEN, OUTPUT); pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_GREEN, HIGH); digitalWrite(LED_RED, HIGH);
  pinMode(53, OUTPUT);                      // SS materiel du Mega doit rester en sortie

  Serial.begin(115200);
  Serial.print(F("Tortues ")); Serial.print(VERSION); Serial.println(F(" - demarrage"));

  tft.InitLCD();
  tft.clrScr();
  touch.InitTouch();                        // configure les broches ; la conversion en pixels est faite par touchRead()
  useNormalColors();
  { char buf[32]; snprintf(buf, sizeof(buf), "Tortues %s - demarrage", VERSION); bootMsg(buf, 10, false); }

  // Horloge
  while (!rtc.begin()) { bootMsg("Horloge introuvable, nouvel essai...", 40, true); delay(2000); }
  rtcLost = rtc.lostPower();
  DateTime now = rtc.now();
  // Pour regler l'heure : decommenter, televerser, puis recommenter et televerser a nouveau.
  // rtc.adjust(DateTime(2026, 11, 1, 12, 0, 0)); rtcLost = false;
  bootMsg(rtcLost ? "Horloge : HEURE PERDUE (pile ?)" : "Horloge OK", 40, rtcLost);

  // Carte SD
  while (!sd.begin(SD_CS, SPI_HALF_SPEED)) { bootMsg("Carte SD absente, nouvel essai...", 65, true); delay(2000); }
  buildFileName(now);
  while (!openForAppend()) { bootMsg("Fichier SD impossible a ouvrir, nouvel essai...", 65, true); delay(2000); sd.begin(SD_CS, SPI_HALF_SPEED); }
  file.close();
  { char buf[48]; snprintf(buf, sizeof(buf), "Carte SD OK  %s  %lu enreg.", fileName, (unsigned long)numRec); bootMsg(buf, 65, false); }
  { char d[40];
    const char *cause = (resetCause & _BV(WDRF)) ? "chien de garde" : (resetCause & _BV(BORF)) ? "baisse de tension"
                      : (resetCause & _BV(EXTRF)) ? "bouton reset / USB" : (resetCause & _BV(PORF)) ? "mise sous tension" : "inconnue";
    snprintf(d, sizeof(d), "%s;cause=%s", VERSION, cause);
    logEvent(F("DEMARRAGE"), d); }
  if (rtcLost) logEvent(F("HORLOGE"), "heure perdue (pile ?)");

  // Capteur
  while (!scd.begin()) { bootMsg("Capteur absent, nouvel essai...", 90, true); delay(2000); }
  Wire.setWireTimeout(25000, true);         // evite un blocage definitif du bus I2C
  scd.enablePeriodMeasure(SCD4X_STOP_PERIODIC_MEASURE);
  bootMsg("Capteur : auto-test (10 s)...", 90, false);
  if (scd.performSelfTest() != 0) { bootMsg("Capteur : auto-test EN ECHEC", 90, true); logEvent(F("CAPTEUR"), "auto-test en echec"); delay(3000); }
  else bootMsg("Capteur OK", 90, false);
  sensorStart();
  delay(1500);

  // Ecran
  drawFrame();
  drawClock(now);
  lastClockMinute = now.minute();
  if (numRec > 0) viewTop = fileBytes - REC_SIZE;
  drawHistory();
  drawScrollIndicator();
  buttonsActive = (numRec >= 2);
  drawButtons(buttonsActive);
  drawBadge24h();
  Serial.println(F("date_heure;temp;hum;co2;flag;niveau;enregistre"));

  // Premiere mesure immediate (affichage seul, pas d'enregistrement)
  now = rtc.now();
  doMeasurement(now, false);
  lastMinuteKey = now.hour() * 60 + now.minute();

  wdt_enable(WDTO_8S);                      // chien de garde : redemarrage si blocage > 8 s
}

// =====================================================================
//  BOUCLE PRINCIPALE (non bloquante)
// =====================================================================
void loop() {
  wdt_reset();
  updateLed();
  handleTouch();

  if (millis() - lastRtcPoll < 250) return;
  lastRtcPoll = millis();
  DateTime now = rtc.now();

  if (now.minute() != lastClockMinute) {
    lastClockMinute = now.minute();
    drawClock(now);
  }
  int16_t key = now.hour() * 60 + now.minute();
  if (key != lastMinuteKey && (now.minute() % MEASURE_EVERY_MIN) == 0) {
    lastMinuteKey = key;
    bool record = (now.minute() % RECORD_EVERY_MIN) == 0;
    doMeasurement(now, record);
  }
}

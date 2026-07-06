/*
  nixieClock — MSF-synchronised IN-12A nixie clock
  =================================================

  Hardware (this repo):
    * Signal board: ATmega328PB @ 16 MHz, DS3231 RTC on I2C (A4/A5),
      Canaduino v3 MSF receiver with its inverted /OUT wired to the MCU
      (assumed A3/PC3 — see MSF_PIN), 16-pin header carrying 4x4 BCD
      lines to the power board.
    * Power board: LT3757 flyback boost to 160 V, 4x IN-12A tubes, each
      behind a K155ID1 BCD decoder. Any BCD input > 9 blanks that tube,
      which this code uses for blanking (TUBE_BLANK).

  Behaviour:
    * The DS3231 is the timekeeper; the display is always driven from it.
    * The MSF decoder runs continuously via a pin-change interrupt. A time
      fix is only trusted after: fixed-bit check + all four parity groups
      + calendar sanity + TWO consecutive frames exactly one minute apart
      (defeats corrupted/mixed reception). The RTC is then set to within
      a few tens of ms of the radio second.
    * The RTC is only rewritten when it disagrees with the radio by >= 2 s
      or at most once per 24 h. Summer/winter changes arrive over the air
      (MSF broadcasts UK civil time) and are corrected within ~3 minutes
      of good reception after the change.
    * If the RTC stops responding, the clock freewheels on the MCU crystal
      (~20 ppm) until it returns. If the time has never been trusted (RTC
      lost power and no radio sync yet) the display blinks while counting,
      to say "not set".
    * Anti-poisoning: once per hour at xx:59:45 a ~13 s slot-machine cycle
      exercises every cathode of every tube; a fallback cycle runs every
      65 min if the time is not set.
    * A 4 s watchdog recovers from any hang; I2C has a bus timeout.

  Flashing (IMPORTANT — the chip is an ATmega328PB, not a plain 328P):
    * Install MiniCore in the Arduino IDE boards manager:
      https://mcudude.github.io/MiniCore/package_MCUdude_MiniCore_index.json
    * Board: ATmega328 / Variant: 328PB / Clock: external 16 MHz.
    * Libraries: Wire (built in) and "DS3231" by Andrew Wickert
      (NorthernWidget) — the same one the earlier sketches used.

  Bring-up checklist:
    1. Flash with ENABLE_SERIAL 1, open Serial Monitor @ 115200, send 'h'.
    2. Send 't': each tube steps through 0..9 with serial commentary.
       Correct TUBE_PINS[][] below until the tubes match the commentary.
    3. Send 'm': raw MSF pulse monitor. With reception you should see one
       off-pulse per second near 100/200/300 ms (500 ms once a minute).
       If the widths look inverted (~700-900 ms), flip
       MSF_PULSE_ACTIVE_HIGH. If nothing arrives, try MSF_PIN A2 — the
       schematic is ambiguous between PC2 and PC3.
    4. Give it 2-5 min of good reception: you should see "MSF frame ok"
       and then "RTC set from MSF".
    5. If your final TUBE_PINS mapping uses pins 0/1 (the UART pins), set
       ENABLE_SERIAL 0 for the production flash — while serial is enabled
       the UART owns those pins and two BCD lines would misbehave.
*/

#include <Wire.h>
#include <DS3231.h>
#include <avr/wdt.h>
#include "MSFDecoder.h"

// ============================ CONFIGURATION ============================

#define ENABLE_SERIAL   1       // set 0 for the final flash if TUBE_PINS uses pins 0/1
#define ENABLE_WATCHDOG 1       // needs a modern bootloader (MiniCore is fine)
#define SERIAL_BAUD     115200

#define MSF_PIN               A3    // PC3 per schematic; try A2 if silent
#define MSF_PULSE_ACTIVE_HIGH true  // /OUT: HIGH while the carrier is off
// If you move MSF_PIN off the A0-A5 port, also change MSF_PCINT_VECTOR
// in MSFDecoder.h (PCINT0_vect for D8-D13, PCINT2_vect for D0-D7).

// MCU pin -> K155ID1 BCD input mapping, one row per tube, left to right.
// Columns are {A, B, C, D} = binary weights {1, 2, 4, 8} (K155ID1 pins
// 3, 6, 7, 4). *** BEST GUESS — verify with the 't' serial command and
// edit this table until the tubes match the commentary. ***
const uint8_t TUBE_PINS[4][4] = {
  {  8,  9, 10, 11 }, // tube 1: hour tens    (PB0..PB3)
  { 12, 13, A0, A1 }, // tube 2: hour units   (PB4, PB5, PC0, PC1)
  {  0,  1,  2,  3 }, // tube 3: minute tens  (PD0..PD3)
  {  4,  5,  6,  7 }, // tube 4: minute units (PD4..PD7)
};

#define TUBE_BLANK 0x0F  // any BCD value > 9 turns all K155ID1 outputs off

const int32_t  SYNC_THRESHOLD_S    = 2;              // rewrite RTC if off by this much
const uint32_t RESYNC_INTERVAL_MS  = 86400000UL;     // ... or at least daily
const uint8_t  POISON_MINUTE       = 59;             // hourly cycle at xx:59:45
const uint8_t  POISON_SECOND      = 45;
const uint32_t POISON_DURATION_MS  = 13000UL;
const uint32_t POISON_FALLBACK_MS  = 65UL * 60UL * 1000UL; // if time never set

// =======================================================================

DS3231     rtc;
MSFDecoder msf;

struct ClockTime
{
  uint8_t y;   // 0-99 (20xx)
  uint8_t mo;  // 1-12
  uint8_t d;   // 1-31
  uint8_t h;   // 0-23
  uint8_t mi;  // 0-59
  uint8_t s;   // 0-59
};

// The "soft clock": a RAM copy of the current time, re-anchored from the
// RTC every 500 ms and ticked from millis() so it freewheels if the RTC
// or the I2C bus dies.
ClockTime     softTime = {0, 1, 1, 0, 0, 0};
unsigned long softAnchor = 0;

bool    rtcResponding = false;  // recent I2C reads succeeded
bool    rtcTrusted    = false;  // responding AND oscillator-stop flag clear
uint8_t rtcFailCount  = 0;

bool          everSynced    = false;  // at least one MSF sync this power-up
unsigned long lastSyncMillis = 0;

bool          havePrevFix   = false;  // MSF double-frame confirmation chain
uint32_t      prevFixMin    = 0;
unsigned long prevFixMillis = 0;

bool          poisonActive     = false;
unsigned long poisonStart      = 0;
unsigned long lastPoisonMillis = 0;

// ---------------------------- date helpers ----------------------------
// Valid for 2000-2099 (every year divisible by 4 is a leap year there).

uint8_t daysInMonth(uint8_t y, uint8_t mo)
{
  static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t n = dim[mo - 1];
  if (mo == 2 && (y & 3) == 0) n++;
  return n;
}

uint16_t days2000(uint8_t y, uint8_t mo, uint8_t d)
{
  static const uint16_t cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  uint16_t days = (uint16_t)y * 365 + (y + 3) / 4 + cum[mo - 1] + (d - 1);
  if (mo > 2 && (y & 3) == 0) days++;
  return days;
}

uint32_t minutes2000(const MSFTime &f)
{
  return (uint32_t)days2000(f.year, f.month, f.day) * 1440UL
       + (uint32_t)f.hour * 60UL + f.minute;
}

uint32_t secs2000(const ClockTime &t)
{
  return (uint32_t)days2000(t.y, t.mo, t.d) * 86400UL
       + (uint32_t)t.h * 3600UL + (uint32_t)t.mi * 60UL + t.s;
}

void addSeconds(ClockTime &t, uint32_t secs)
{
  uint32_t v = t.s + secs;
  t.s = v % 60; v /= 60;
  v += t.mi;
  t.mi = v % 60; v /= 60;
  v += t.h;
  t.h = v % 24; v /= 24;
  while (v--)
  {
    if (++t.d > daysInMonth(t.y, t.mo))
    {
      t.d = 1;
      if (++t.mo > 12) { t.mo = 1; if (++t.y > 99) t.y = 0; }
    }
  }
}

// ---------------------------- tube driving ----------------------------

void setTube(uint8_t tube, uint8_t value)
{
  for (uint8_t b = 0; b < 4; b++)
    digitalWrite(TUBE_PINS[tube][b], (value >> b) & 1);
}

void blankAll()
{
  for (uint8_t t = 0; t < 4; t++) setTube(t, TUBE_BLANK);
}

void showTime(uint8_t h, uint8_t mi)
{
  setTube(0, h / 10);
  setTube(1, h % 10);
  setTube(2, mi / 10);
  setTube(3, mi % 10);
}

// ------------------------------- RTC ----------------------------------

bool timeValid()
{
  // Trust the RTC when its oscillator never stopped; if the RTC has died
  // but we synced from the radio this power-up, the freewheeling soft
  // clock is still good.
  return rtcTrusted || (everSynced && !rtcResponding);
}

bool readRTC(ClockTime &t)
{
  bool h12, pm, century;
  t.s  = rtc.getSecond();
  t.mi = rtc.getMinute();
  t.h  = rtc.getHour(h12, pm);
  t.d  = rtc.getDate();
  t.mo = rtc.getMonth(century);
  t.y  = rtc.getYear();
  // A dead/absent bus yields out-of-range BCD; treat anything insane as a
  // failed read so the soft clock freewheels instead of jumping.
  if (t.s > 59 || t.mi > 59 || t.h > 23 || t.y > 99) return false;
  if (t.mo < 1 || t.mo > 12 || t.d < 1 || t.d > daysInMonth(t.y, t.mo)) return false;
  return true;
}

void serviceRTC(bool force)
{
  static unsigned long last = 0;
  if (!force && millis() - last < 500UL) return;
  last = millis();

  ClockTime t;
  if (readRTC(t))
  {
    rtcFailCount  = 0;
    rtcResponding = true;
    rtcTrusted    = rtc.oscillatorCheck(); // false if the RTC ever lost power
    softTime      = t;
    softAnchor    = millis();
  }
  else
  {
    if (rtcFailCount < 250) rtcFailCount++;
    if (rtcFailCount >= 6) { rtcResponding = false; rtcTrusted = false; }
    if (rtcFailCount % 20 == 0)
    {
      Wire.begin(); // try to recover a wedged bus
#if defined(WIRE_HAS_TIMEOUT)
      Wire.setWireTimeout(25000, true);
      Wire.clearWireTimeoutFlag();
#endif
    }
  }
}

void serviceSoftClock()
{
  while (millis() - softAnchor >= 1000UL)
  {
    softAnchor += 1000UL;
    addSeconds(softTime, 1);
  }
}

// ------------------------------- MSF ----------------------------------

#if ENABLE_SERIAL
void print2(uint8_t v)
{
  if (v < 10) Serial.print('0');
  Serial.print(v);
}

void printClock(const ClockTime &t)
{
  Serial.print(F("20")); print2(t.y);
  Serial.print('-'); print2(t.mo);
  Serial.print('-'); print2(t.d);
  Serial.print(' '); print2(t.h);
  Serial.print(':'); print2(t.mi);
  Serial.print(':'); print2(t.s);
}
#endif

// Write a confirmed radio fix into the RTC, aligned to the second.
void applyFix(const MSFTime &f, unsigned long fixMillis)
{
  // Spin (max ~1 s) to a whole-second boundary of the radio epoch so the
  // DS3231's internal second-countdown restarts in phase with the signal.
  unsigned long el;
  do {
#if ENABLE_WATCHDOG
    wdt_reset();
#endif
    el = millis() - fixMillis;
  } while (el % 1000UL > 30UL);

  ClockTime t = { f.year, f.month, f.day, f.hour, f.minute, 0 };
  uint16_t d0 = days2000(t.y, t.mo, t.d);
  addSeconds(t, el / 1000UL);
  uint8_t dow = (f.dow + (uint8_t)(days2000(t.y, t.mo, t.d) - d0)) % 7;

  rtc.setClockMode(false); // 24 h
  rtc.setYear(t.y);
  rtc.setMonth(t.mo);
  rtc.setDate(t.d);
  rtc.setDoW(dow + 1);     // 1 = Sunday
  rtc.setHour(t.h);
  rtc.setMinute(t.mi);
  rtc.setSecond(t.s);      // written last; also clears the oscillator-stop flag

  softTime   = t;
  softAnchor = millis() - (el % 1000UL);

#if ENABLE_SERIAL
  Serial.print(F("RTC set from MSF: "));
  printClock(t);
  Serial.println(f.summer ? F(" (BST)") : F(" (GMT)"));
#endif
}

void serviceMSF()
{
  MSFTime f;
  unsigned long tf;
  if (!msf.takeFix(f, tf)) return;

  uint32_t fixMin = minutes2000(f);

  // Require two consecutive frames exactly one minute apart before
  // touching the RTC — a corrupted frame that beat the parity checks
  // will practically never do so twice in a row *and* be consistent.
  bool confirmed = false;
  if (havePrevFix)
  {
    unsigned long gap = tf - prevFixMillis;
    confirmed = (gap >= 59000UL && gap <= 61000UL) && (fixMin == prevFixMin + 1);
  }
  havePrevFix   = true;
  prevFixMin    = fixMin;
  prevFixMillis = tf;

#if ENABLE_SERIAL
  Serial.print(F("MSF frame ok: 20")); print2(f.year);
  Serial.print('-'); print2(f.month);
  Serial.print('-'); print2(f.day);
  Serial.print(' '); print2(f.hour);
  Serial.print(':'); print2(f.minute);
  Serial.println(confirmed ? F(" [confirmed]") : F(" [waiting for 2nd frame]"));
#endif

  if (!confirmed) return;

  uint32_t msfNow = fixMin * 60UL + (millis() - tf) / 1000UL;
  int32_t  diff   = (int32_t)(msfNow - secs2000(softTime));

  bool need = !timeValid()
           || diff >= SYNC_THRESHOLD_S || diff <= -SYNC_THRESHOLD_S
           || !everSynced
           || (millis() - lastSyncMillis) >= RESYNC_INTERVAL_MS;
  if (need) applyFix(f, tf);

  // Even when no rewrite was needed, the radio just vouched for the RTC.
  everSynced     = true;
  lastSyncMillis = millis();
}

// --------------------------- anti-poisoning ---------------------------

void startPoison()
{
  poisonActive = true;
  poisonStart  = millis();
}

void servicePoison()
{
  if (poisonActive)
  {
    if (millis() - poisonStart >= POISON_DURATION_MS)
    {
      poisonActive     = false;
      lastPoisonMillis = millis();
    }
    return;
  }
  if (timeValid() && softTime.mi == POISON_MINUTE && softTime.s == POISON_SECOND)
    startPoison();
  else if (millis() - lastPoisonMillis >= POISON_FALLBACK_MS)
    startPoison(); // keeps cathodes exercised even while unsynchronised
}

// ------------------------------ display -------------------------------

void serviceDisplay()
{
  if (poisonActive)
  {
    uint16_t step = (uint16_t)((millis() - poisonStart) / 70UL);
    for (uint8_t t = 0; t < 4; t++)
      setTube(t, (step + t * 3) % 10);
    return;
  }

  bool show = true;
  if (!timeValid()) show = (millis() % 1000UL) < 700UL; // blink = "not set"

  if (show) showTime(softTime.h, softTime.mi);
  else      blankAll();
}

// ------------------------- serial diagnostics -------------------------

#if ENABLE_SERIAL

void printHelp()
{
  Serial.println(F("Commands: i=status  t=tube mapping test  m=MSF pulse monitor (30s)  p=poison cycle now  h=help"));
}

void printStatus()
{
  Serial.println(F("--- nixieClock status ---"));
  Serial.print(F("Soft time : ")); printClock(softTime); Serial.println();
  Serial.print(F("Time valid: ")); Serial.println(timeValid() ? F("yes") : F("no (display blinking)"));
  Serial.print(F("RTC       : "));
  Serial.print(rtcResponding ? F("responding") : F("NOT RESPONDING"));
  Serial.print(F(", oscillator "));
  Serial.println(rtcTrusted ? F("ok") : F("stopped at some point (untrusted until sync)"));
  Serial.print(F("MSF       : signal "));
  Serial.print(msf.hasSignal() ? F("yes") : F("NO"));
  Serial.print(F(", frame bit ")); Serial.print(msf.bitCount());
  Serial.print(F(", valid frames ")); Serial.print(msf.goodFixes());
  Serial.print(F(", rejected ")); Serial.println(msf.rejects());
  Serial.print(F("Last sync : "));
  if (everSynced)
  {
    Serial.print((millis() - lastSyncMillis) / 60000UL);
    Serial.println(F(" min ago"));
  }
  else Serial.println(F("never (this power-up)"));
}

// Steps every tube through 0..9 so TUBE_PINS[][] can be verified.
void tubeTest()
{
  while (Serial.available()) Serial.read();
  Serial.println(F("Tube mapping test — press any key to abort."));
  for (uint8_t t = 0; t < 4 && !Serial.available(); t++)
  {
    blankAll();
    for (uint8_t v = 0; v < 10 && !Serial.available(); v++)
    {
      setTube(t, v);
      Serial.print(F("Tube ")); Serial.print(t + 1);
      Serial.print(F(" (row ")); Serial.print(t);
      Serial.print(F(" of TUBE_PINS) should show ")); Serial.println(v);
      unsigned long t0 = millis();
      while (millis() - t0 < 800UL && !Serial.available())
      {
#if ENABLE_WATCHDOG
        wdt_reset();
#endif
      }
    }
  }
  while (Serial.available()) Serial.read();
  blankAll();
  Serial.println(F("Test done. If digits were wrong, edit TUBE_PINS[][] and reflash."));
}

// Prints every classified MSF pulse for 30 s.
void msfMonitor()
{
  while (Serial.available()) Serial.read();
  Serial.println(F("MSF pulse monitor for 30 s (off ~100/200/300, 500=minute marker) — any key to abort."));
  uint16_t lastCount;
  noInterrupts(); lastCount = msf.dbgPulseCount; interrupts();
  unsigned long t0 = millis();
  while (millis() - t0 < 30000UL && !Serial.available())
  {
#if ENABLE_WATCHDOG
    wdt_reset();
#endif
    int16_t off, on;
    uint16_t count;
    noInterrupts();
    off = msf.dbgOffWidth; on = msf.dbgOnWidth; count = msf.dbgPulseCount;
    interrupts();
    if (count != lastCount)
    {
      lastCount = count;
      Serial.print(F("off=")); Serial.print(off);
      Serial.print(F(" ms, on=")); Serial.print(on);
      Serial.print(F(" ms, frame bit ")); Serial.println(msf.bitCount());
    }
  }
  while (Serial.available()) Serial.read();
  Serial.println(F("Monitor done."));
}

void serviceSerial()
{
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c)
  {
    case 'i': printStatus(); break;
    case 't': tubeTest();    break;
    case 'm': msfMonitor();  break;
    case 'p': startPoison(); break;
    case 'h':
    case '?': printHelp();   break;
    default:  break; // ignore line endings etc.
  }
}

#endif // ENABLE_SERIAL

// ------------------------------- setup --------------------------------

void setup()
{
  MCUSR = 0;      // make sure a watchdog reset can't turn into a boot loop
  wdt_disable();

  for (uint8_t t = 0; t < 4; t++)
    for (uint8_t b = 0; b < 4; b++)
      pinMode(TUBE_PINS[t][b], OUTPUT);
  blankAll();

#if ENABLE_SERIAL
  Serial.begin(SERIAL_BAUD);
  Serial.println(F("nixieClock starting"));
  printHelp();
#endif

  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000, true); // never let a stuck bus hang the loop
#endif

  msf.begin(MSF_PIN, MSF_PULSE_ACTIVE_HIGH);

  // power-on sweep: proves every cathode of every tube works
  for (uint8_t v = 0; v < 10; v++)
  {
    for (uint8_t t = 0; t < 4; t++) setTube(t, v);
    delay(180);
  }
  blankAll();

  softAnchor       = millis();
  lastPoisonMillis = millis();
  serviceRTC(true); // adopt battery-backed RTC time immediately if sane

#if ENABLE_SERIAL
  Serial.print(F("Boot time from RTC: "));
  printClock(softTime);
  Serial.println(rtcTrusted ? F(" (trusted)") : F(" (NOT trusted — waiting for MSF)"));
#endif

#if ENABLE_WATCHDOG
  wdt_enable(WDTO_4S);
#endif
}

void loop()
{
#if ENABLE_WATCHDOG
  wdt_reset();
#endif
  serviceSoftClock();
  serviceRTC(false);
  serviceMSF();
  servicePoison();
  serviceDisplay();
#if ENABLE_SERIAL
  serviceSerial();
#endif
}

// MSFDecoder — see MSFDecoder.h for credits and overview.

#include "MSFDecoder.h"

#define PULSE_MARGIN_MS  30  // leeway allowed on the off-pulse widths
#define PULSE_OFF_OFFSET 20  // receivers of this family deliver slightly
                             // short 'off' periods; tune with the 'm' monitor
#define PULSE_GLITCH_MS  30  // transitions shorter than this are noise

static MSFDecoder *sInstance = nullptr;

ISR(MSF_PCINT_VECTOR)
{
  if (sInstance) sInstance->handleEdge();
}

void MSFDecoder::begin(uint8_t pin, bool pulseActiveHigh)
{
  m_pin = pin;
  m_pulseActiveHigh = pulseActiveHigh;

  m_reading = false;
  m_offStart = m_onStart = m_prevOffStart = m_prevOnStart = 0;
  m_onWidth = 0;
  m_lastEdgeMillis = 0;
  m_bitIndex = 0;
  for (uint8_t i = 0; i < 8; i++) { m_aBits[i] = 0; m_bBits[i] = 0; }

  m_fixSeq = 0;
  m_takenSeq = 0;
  m_fixCount = 0;
  m_rejectCount = 0;
  dbgOffWidth = 0;
  dbgOnWidth = 0;
  dbgPulseCount = 0;

  pinMode(pin, INPUT);
  sInstance = this;

  volatile uint8_t *pcicr = digitalPinToPCICR(pin);
  volatile uint8_t *pcmsk = digitalPinToPCMSK(pin);
  if (pcicr && pcmsk)
  {
    *pcmsk |= _BV(digitalPinToPCMSKbit(pin));
    *pcicr |= _BV(digitalPinToPCICRbit(pin));
  }
}

bool MSFDecoder::takeFix(MSFTime &out, unsigned long &fixMillis)
{
  if (m_fixSeq == m_takenSeq) return false;
  noInterrupts();
  out = m_fix;
  fixMillis = m_fixMillis;
  m_takenSeq = m_fixSeq;
  interrupts();
  return true;
}

bool MSFDecoder::hasSignal() const
{
  noInterrupts();
  unsigned long t = m_lastEdgeMillis;
  interrupts();
  return (t != 0) && (millis() - t < 3000UL);
}

// Runs on every edge of the receiver output. Classifies completed
// carrier-off pulses per the NPL format:
//   500 ms off                          -> minute marker
//   300 ms off                          -> bits 1 1
//   200 ms off                          -> bits 1 0
//   100 ms off + ~900 ms on             -> bits 0 0
//   100 ms off + 100 ms on + 100 ms off -> bits 0 1
void MSFDecoder::handleEdge()
{
  bool carrierOff = ((digitalRead(m_pin) != 0) == m_pulseActiveHigh);
  unsigned long now = millis();
  m_lastEdgeMillis = now;

  if (carrierOff) // carrier just dropped: start timing the off pulse
  {
    if (now - m_onStart < PULSE_GLITCH_MS)
    {
      m_onStart = m_prevOnStart; // ignore this transition and the previous one
      return;
    }
    m_prevOffStart = m_offStart;
    m_offStart = now;
    m_onWidth = (long)(m_offStart - m_onStart);
    return;
  }

  // carrier restored: classify the completed off pulse
  if (now - m_offStart < PULSE_GLITCH_MS)
  {
    m_offStart = m_prevOffStart; // ignore this transition and the previous one
    return;
  }
  m_prevOnStart = m_onStart;
  m_onStart = now;

  long offWidth = (long)(now - m_offStart) - PULSE_OFF_OFFSET;
  long onWidth  = m_onWidth;

  dbgOffWidth = (int16_t)offWidth;
  dbgOnWidth  = (int16_t)((onWidth > 32767L) ? 32767L : onWidth);
  dbgPulseCount++;

  bool is500 = labs(offWidth - 500) < PULSE_MARGIN_MS;
  bool is300 = labs(offWidth - 300) < PULSE_MARGIN_MS;
  bool is200 = labs(offWidth - 200) < PULSE_MARGIN_MS;
  bool is100 = labs(offWidth - 100) < PULSE_MARGIN_MS;

  bool onWas100    = (onWidth > 5)   && (onWidth < 200);
  bool onWasNormal = (onWidth > 400) && (onWidth < 900L + PULSE_MARGIN_MS);

  if (is500) // minute marker: decode what we collected, start a fresh frame
  {
    if (m_reading) doDecode();
    m_reading = true;
    m_bitIndex = 1; // NPL numbers bits from 1
    return;
  }

  if (!m_reading) return;

  if (m_bitIndex < 60 && onWasNormal && (is100 || is200 || is300))
  {
    if (is100) { setBit(m_aBits, m_bitIndex, false); setBit(m_bBits, m_bitIndex, false); m_bitIndex++; }
    if (is200) { setBit(m_aBits, m_bitIndex, true);  setBit(m_bBits, m_bitIndex, false); m_bitIndex++; }
    if (is300) { setBit(m_aBits, m_bitIndex, true);  setBit(m_bBits, m_bitIndex, true);  m_bitIndex++; }
  }
  else if (m_bitIndex > 1 && m_bitIndex < 60 && onWas100 && is100)
  {
    // second 100 ms dip right after a 100 ms dip: B bit of the preceding pair
    setBit(m_bBits, m_bitIndex - 1, true);
  }
  else // malformed pulse: abandon this minute entirely
  {
    m_reading = false;
    m_bitIndex = 0;
    m_rejectCount++;
  }
}

static const uint8_t kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

void MSFDecoder::doDecode()
{
  if (m_bitIndex != 60) { m_rejectCount++; return; } // must have all 59 bits
  if (!checkValid())    { m_rejectCount++; return; }

  MSFTime f;
  f.year   = decodeBCD(m_aBits, 24, 17);
  f.month  = decodeBCD(m_aBits, 29, 25);
  f.day    = decodeBCD(m_aBits, 35, 30);
  f.dow    = decodeBCD(m_aBits, 38, 36);
  f.hour   = decodeBCD(m_aBits, 44, 39);
  f.minute = decodeBCD(m_aBits, 51, 45);
  f.summer = getBit(m_bBits, 58);

  // calendar sanity on top of parity (valid for 2000-2099 leap rule)
  uint8_t dim = (f.month >= 1 && f.month <= 12) ? kDaysInMonth[f.month - 1] : 0;
  if (f.month == 2 && (f.year & 3) == 0) dim++;
  if (f.year > 99 || dim == 0 || f.day < 1 || f.day > dim ||
      f.dow > 6 || f.hour > 23 || f.minute > 59)
  {
    m_rejectCount++;
    return;
  }

  m_fix = f;
  // The frame describes the minute starting at the marker we just measured;
  // that second-00 epoch was ~500 ms ago (the width of the marker pulse).
  m_fixMillis = millis() - 500UL;
  m_fixSeq++;
  m_fixCount++;
}

bool MSFDecoder::checkValid()
{
  // fixed pattern 52A..59A = 0 1 1 1 1 1 1 0
  if ( getBit(m_aBits, 52)) return false;
  if (!getBit(m_aBits, 53)) return false;
  if (!getBit(m_aBits, 54)) return false;
  if (!getBit(m_aBits, 55)) return false;
  if (!getBit(m_aBits, 56)) return false;
  if (!getBit(m_aBits, 57)) return false;
  if (!getBit(m_aBits, 58)) return false;
  if ( getBit(m_aBits, 59)) return false;
  // four odd-parity groups
  if (!checkParity(m_aBits, 17, 24, getBit(m_bBits, 54))) return false;
  if (!checkParity(m_aBits, 25, 35, getBit(m_bBits, 55))) return false;
  if (!checkParity(m_aBits, 36, 38, getBit(m_bBits, 56))) return false;
  if (!checkParity(m_aBits, 39, 51, getBit(m_bBits, 57))) return false;
  return true;
}

bool MSFDecoder::checkParity(uint8_t *bits, uint8_t from, uint8_t to, bool p)
{
  uint8_t set = p ? 1 : 0;
  for (uint8_t b = from; b <= to; b++)
    if (getBit(bits, b)) set++;
  return (set & 0x01) != 0; // must come out odd
}

void MSFDecoder::setBit(uint8_t *bits, uint8_t i, bool v)
{
  uint8_t mask = 1 << (i & 0x07);
  if (v) bits[i >> 3] |= mask;
  else   bits[i >> 3] &= ~mask;
}

bool MSFDecoder::getBit(uint8_t *bits, uint8_t i)
{
  return (bits[i >> 3] & (1 << (i & 0x07))) != 0;
}

static const uint8_t kBCD[8] = {1, 2, 4, 8, 10, 20, 40, 80};

uint8_t MSFDecoder::decodeBCD(uint8_t *bits, uint8_t lsb, uint8_t msb)
{
  uint8_t result = 0;
  uint8_t d = 0;
  for (uint8_t b = lsb; b >= msb; b--, d++)
    if (getBit(bits, b)) result += kBCD[d];
  return result;
}

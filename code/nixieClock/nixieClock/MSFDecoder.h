// MSFDecoder — decodes the NPL MSF 60 kHz time signal (Anthorn, UK).
//
// Pulse-classification core based on MSFTime by Jarkman (01/2011),
// http://www.jarkman.co.uk/catalog/robots/msftime.htm — all kudos to Jarkman.
//
// Reworked for the nixie signal board (ATmega328PB):
//   * uses a pin-change interrupt instead of INT0/INT1, so the receiver can
//     sit on PC3/A3 as routed on the PCB (set MSF_PCINT_VECTOR to the PCINT
//     group of the pin you use)
//   * configurable polarity — the board feeds the Canaduino receiver's
//     inverted /OUT output to the MCU
//   * every decoded frame is checked: fixed bit pattern 52A..59A, all four
//     parity groups, and field range / calendar validity
//   * fixes are handed to the main loop atomically via a sequence counter

#ifndef MSF_DECODER_H
#define MSF_DECODER_H

#include <Arduino.h>

// Pin-change interrupt vector covering the MSF input pin:
//   PCINT0_vect = PB0..PB7 (D8-D13), PCINT1_vect = PC0..PC6 (A0-A5),
//   PCINT2_vect = PD0..PD7 (D0-D7)
#ifndef MSF_PCINT_VECTOR
#define MSF_PCINT_VECTOR PCINT1_vect
#endif

struct MSFTime
{
  uint8_t year;    // 0-99 (20xx)
  uint8_t month;   // 1-12
  uint8_t day;     // 1-31
  uint8_t dow;     // 0-6, 0 = Sunday
  uint8_t hour;    // 0-23
  uint8_t minute;  // 0-59
  bool    summer;  // true while UK summer time (BST) is in effect (bit 58B)
};

class MSFDecoder
{
public:
  // pulseActiveHigh: true if the input reads HIGH while the carrier is OFF.
  // True for the /OUT output wired on the signal board.
  void begin(uint8_t pin, bool pulseActiveHigh);

  void handleEdge(); // called from the pin-change ISR only

  // Returns true exactly once per newly decoded valid frame. fixMillis is
  // the millis() timestamp of the second-00 epoch the fix refers to.
  bool takeFix(MSFTime &out, unsigned long &fixMillis);

  bool     hasSignal() const;                     // edges within the last 3 s
  uint8_t  bitCount() const  { return m_bitIndex; }
  uint16_t goodFixes() const { return m_fixCount; }
  uint16_t rejects() const   { return m_rejectCount; }

  // debug taps for the serial pulse monitor ('m' command)
  volatile int16_t  dbgOffWidth;
  volatile int16_t  dbgOnWidth;
  volatile uint16_t dbgPulseCount;

private:
  void    doDecode();
  bool    checkValid();
  bool    checkParity(uint8_t *bits, uint8_t from, uint8_t to, bool p);
  void    setBit(uint8_t *bits, uint8_t i, bool v);
  bool    getBit(uint8_t *bits, uint8_t i);
  uint8_t decodeBCD(uint8_t *bits, uint8_t lsb, uint8_t msb);

  uint8_t m_pin;
  bool    m_pulseActiveHigh;

  // ISR-side state
  bool          m_reading;
  unsigned long m_offStart, m_onStart, m_prevOffStart, m_prevOnStart;
  long          m_onWidth;
  uint8_t       m_aBits[8];
  uint8_t       m_bBits[8];

  volatile unsigned long m_lastEdgeMillis;
  volatile uint8_t       m_bitIndex;

  // handover to main loop (copied under interrupts-off in takeFix)
  MSFTime                m_fix;
  unsigned long          m_fixMillis;
  volatile uint8_t       m_fixSeq;
  uint8_t                m_takenSeq;

  volatile uint16_t m_fixCount, m_rejectCount;
};

#endif // MSF_DECODER_H

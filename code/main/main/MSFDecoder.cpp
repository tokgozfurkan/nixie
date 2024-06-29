// MSFDecoder.

// Originally written as MSFTime by Jarkman, 01/2011 (see http://www.jarkman.co.uk/catalog/robots/msftime.htm).

// Decodes MSF time signals from a receiver like this:
//  http://www.pvelectronics.co.uk/index.php?main_page=product_info&cPath=9&products_id=2

// I have butchered the library to make it a bit more "passive", and to remove dependency on the Time library.
// I just needed a library that would listen on a particular input pin (courtesy of CHANGE interrupts), and store a decoded time.
// This is then grabbed by the host code and used to adjust an RTC module on a regular basis.

// The main decoder logic is fundamentally unchanged - all Kudos to Jarkman, please!

// 9th March 2013

#include "Arduino.h"
#include "MSFDecoder.h"

#define PULSE_MARGIN 30     // The leeway we allow our pulse lengths

#define PULSE_OFF_OFFSET 20 // arbitrary offset to compensate for assymetric behaviour of my receiver,
                            // which tends to deliver an 'off' period that is too short

#define PULSE_IGNORE_WIDTH 30 // just ignore very short pulses

MSFDecoder *sMSF = NULL; // pointer to singleton

MSFDecoder::MSFDecoder()
{
  m_bHasValidTime = false;
  m_bIsReading = false;
}

void sStateChange() // static function for the interrupt handler
{
  if (sMSF) sMSF->stateChange();
}

void MSFDecoder::init()
{
  m_bHasValidTime = false;
  mOffStartMillis = 0;
  mOnStartMillis = 0;
  mPrevOffStartMillis = 0;
  mPrevOnStartMillis = 0;
  mOnWidth = 0;
  mFixMillis = 0;
  m_bIsReading = false;
  m_iBitIndex = 0;
  m_iGoodPulses = 0;

  for (int i = 0; i < 7; i++)
  {
    mABits[i] = 0;
    mBBits[i] = 0;
  }

  sMSF = this; // singleton pointer

  m_iInputPin = 3;
  pinMode(m_iInputPin, INPUT);
  attachInterrupt(1, sStateChange, CHANGE);
}

//byte oldVal = 3;

void MSFDecoder::stateChange()  // interrupt routine called on every state change
{
  byte val = digitalRead(m_iInputPin);
  //if (val == oldVal) return;
  //oldVal = val;

  long millisNow = millis();

  // see here:
  // http://www.pvelectronics.co.uk/rftime/msf/MSF_Time_Date_Code.pdf
  // for an explanation of the format

  // Carrier goes off for 100, 200, 300, or 500 mS during every second

  // Our input is inverted by our receiver, so val != 0 when carrier is off

  if (val != 0) // carrier is off, start timing
  {
  	if (millisNow - mOnStartMillis < PULSE_IGNORE_WIDTH)
    {
      // ignore this transition plus the previous one        
      mOnStartMillis = mPrevOnStartMillis;
      return;
    }
      
    mPrevOffStartMillis = mOffStartMillis;
    mOffStartMillis = millisNow;

    mOnWidth = mOffStartMillis - mOnStartMillis;
    return; 
  }
    
  if (millisNow - mOffStartMillis < PULSE_IGNORE_WIDTH)
  {
    // ignore this transition plus the previous one
    mOffStartMillis = mPrevOffStartMillis;
    return;
  }
    
  mPrevOnStartMillis = mOnStartMillis;
  mOnStartMillis = millisNow;
  
  long offWidth = millisNow - mOffStartMillis - PULSE_OFF_OFFSET;

  /* check the width of the off-pulse; according to the specifications, a
   * pulse must be 0.1 or 0.2 or 0.3 or 0.5 seconds
   */

  bool is500 = abs(offWidth-500) < PULSE_MARGIN;
  bool is300 = abs(offWidth-300) < PULSE_MARGIN;
  bool is200 = abs(offWidth-200) < PULSE_MARGIN;
  bool is100 = abs(offWidth-100) < PULSE_MARGIN;

  long onWidth = mOnWidth;

  bool onWas100 =  (onWidth > 5) && (onWidth < 200);

  bool onWasNormal = (onWidth > 400) && (onWidth < (900 + PULSE_MARGIN));

  if ((onWasNormal || onWas100) && (is100 || is200 || is300 || is500))
  {
    m_iGoodPulses++;
  }
  else
  {
    m_iGoodPulses = 0;
  }
    
  /*
    Cases:
    a 500mS carrier-off marks the start of a minute
    a 300mS carrier-off means bits 1 1
    a 200mS carrier-off means bits 1 0
    a 100mS carrier-off followed by a 900mS carrier-on means bits 0 0
    a 100mS carrier-off followed by a 100mS carrier-on followed by a 100mS carrier-off means bits 0 1
  */

  if (is500) // minute marker
  {
    if (m_bIsReading) doDecode();

    m_bIsReading = true; // and get ready to read the next minute's worth
    m_iBitIndex = 1;  // the NPL docs number bits from 1, so we will too
  }
  else
  if (m_bIsReading)
  {
    if (m_iBitIndex < 60 && onWasNormal && (is100 || is200 || is300)) // we got a sensible pair of bits, 00 or 01 or 11
    {
      if (is100)
      {
        setBit(mABits, m_iBitIndex, false);
        setBit(mBBits, m_iBitIndex++, false);
      }
      if (is200)
      {
        setBit(mABits, m_iBitIndex, true);
        setBit(mBBits, m_iBitIndex++, false);
      }
      if (is300)
      {
        setBit(mABits, m_iBitIndex, true);
        setBit(mBBits, m_iBitIndex++, true);
      }
    }
    else
    if (m_iBitIndex < 60 && onWas100 && is100 && m_iBitIndex > 0) // tricky - we got a second bit for the preceding pair
    {
      setBit(mBBits, m_iBitIndex - 1, true);
    }
    else // bad pulse, give up
    {
      m_bIsReading = false;
      m_iBitIndex = 0;
    }
  }
}

void MSFDecoder::doDecode()
{
	m_bHasValidTime = false;
	
    if (m_iBitIndex != 60) return; // there are always 59 bits, barring leap-seconds
    
    if (!checkValid()) return;

    mFixMillis = millis() - 500L;

    m_iYear    = decodeBCD(mABits, 24, 17);    // 0-99
    m_iMonth   = decodeBCD(mABits, 29, 25);    // 1-12
    m_iDay     = decodeBCD(mABits, 35, 30);    // 1-31
    m_iDOW     = decodeBCD(mABits, 38, 36);
    m_iHour    = decodeBCD(mABits, 44, 39);    // 0-23
    m_iMinute  = decodeBCD(mABits, 51, 45);    // 0-59
    m_bHasValidTime = true;
    mSignalReceivedMillis = millis(); // Record the exact time of valid signal reception
}

bool MSFDecoder::checkValid()
{
  if ( getBit(mABits, 52)) return(false);
  if (!getBit(mABits, 53)) return(false);
  if (!getBit(mABits, 54)) return(false);
  if (!getBit(mABits, 55)) return(false);  
  if (!getBit(mABits, 56)) return(false);
  if (!getBit(mABits, 57)) return(false);
  if (!getBit(mABits, 58)) return(false);
  if ( getBit(mABits, 59)) return(false);
  if (!checkParity(mABits, 17, 24, getBit(mBBits, 54))) return(false);
  if (!checkParity(mABits, 25, 35, getBit(mBBits, 55))) return(false);
  if (!checkParity(mABits, 36, 38, getBit(mBBits, 56))) return(false);
  if (!checkParity(mABits, 39, 51, getBit(mBBits, 57))) return(false);

  return(true);
}

bool MSFDecoder::checkParity(byte *bits, int from, int to, bool p)
{
  int set = 0;
  int b;
  for (b = from; b <= to; b++)
  {
    if (getBit(bits, b)) set++;
  }

  if (p) set++;

  if (set & 0x01) return(true);// must be an odd number of set bits

  return(false);
}

bool MSFDecoder::getHasCarrier()
{
  if ((millis() - mOffStartMillis) >= 5000L) return(false);
  return(true);
}

void MSFDecoder::setBit(byte* bits, int bitIndex, bool bSet) // sets bit if bSet is true, or clears it otherwise
{
  byte mask = 1 << (bitIndex & 0x7);

  if (bSet) bits[bitIndex>>3] = bits[bitIndex>>3] | mask;
  else      bits[bitIndex>>3] = bits[bitIndex>>3] & ( ~ mask );
}

bool MSFDecoder::getBit(byte* bits, int bitIndex)
{
  byte mask = 1 << (bitIndex & 0x7);

  return (bits[bitIndex>>3] & mask) != 0;
}

byte BCD[] = {1, 2, 4, 8, 10, 20, 40, 80};

byte MSFDecoder::decodeBCD( byte *bits, byte lsb, byte msb )
{
  byte result = 0;

  byte b = lsb;
  byte d = 0;

  for( ; b >= msb; b --, d ++ )
  {
    if( getBit( bits, b ))
      result += BCD[ d ];
  }

  return result;
}




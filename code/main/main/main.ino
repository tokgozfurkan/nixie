#include "MSFDecoder.h"
#include <DS3231.h>
#include <Wire.h>

MSFDecoder msf;
DS3231 myRTC;

// Nixie tube pins for one digit (seconds ones)
int segmentPins[4] = {11, 10, 9, 8};

// Nixie tube table
boolean segments[10][4] = {
  {0, 0, 0, 0}, // 0
  {0, 0, 0, 1}, // 1
  {0, 0, 1, 0}, // 2
  {0, 0, 1, 1}, // 3
  {0, 1, 0, 0}, // 4
  {0, 1, 0, 1}, // 5
  {0, 1, 1, 0}, // 6
  {0, 1, 1, 1}, // 7
  {1, 0, 0, 0}, // 8
  {1, 0, 0, 1}  // 9
};

// Variables for time keeping
unsigned long lastSyncTime = 0;
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 1000; // Update every second

void setup() {
  Serial.begin(9600);
  Serial.println(F("MSF Decoder started"));

  // Initialize Nixie tube pins
  for (int i = 0; i < 4; i++) {
    pinMode(segmentPins[i], OUTPUT);
  }

  msf.init(); // Initialize the MSF decoder
  Wire.begin(); // Initialize I2C communication for RTC
}

void loop() {
  if (msf.m_bHasValidTime) {
    syncRTCWithMSF();
    msf.m_bHasValidTime = false; // Reset flag after syncing
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdateTime >= updateInterval) {
    updateDisplayFromRTC();
    lastUpdateTime = currentMillis;
  }
}

void syncRTCWithMSF() {
  unsigned long currentMillis = millis();
  unsigned long millisSinceSignal = currentMillis - msf.mSignalReceivedMillis;
  
  // The MSF signal is received at the start of a minute, so add 2000ms to account for that
  millisSinceSignal += 2000;

  // Calculate seconds and remaining milliseconds
  int seconds = millisSinceSignal / 1000;
  int milliseconds = millisSinceSignal % 1000;

  // Account for processing delay
  const int PROCESSING_DELAY_MS = 25;
  milliseconds += PROCESSING_DELAY_MS;

  // Adjust for overflow
  if (milliseconds >= 1000) {
    seconds++;
    milliseconds -= 1000;
  }

  // If we're in the next minute already, adjust accordingly
  if (seconds >= 60) {
    msf.m_iMinute++;
    if (msf.m_iMinute >= 60) {
      msf.m_iMinute = 0;
      msf.m_iHour++;
      if (msf.m_iHour >= 24) {
        msf.m_iHour = 0;
        // Note: Day rollover is not handled here for simplicity
      }
    }
    seconds -= 60;
  }

  // Set RTC time
  myRTC.setClockMode(false);  // set to 24h
  myRTC.setYear(msf.m_iYear);
  myRTC.setMonth(msf.m_iMonth);
  myRTC.setDate(msf.m_iDay);
  myRTC.setDoW(msf.m_iDOW);
  myRTC.setHour(msf.m_iHour);
  myRTC.setMinute(msf.m_iMinute);
  myRTC.setSecond(seconds);

  // If your RTC supports sub-second precision, use it
  // myRTC.setSubSecond(milliseconds);

  printDateTime();
  printRTCTime();

  // Print detailed timing information for debugging
  Serial.print("Milliseconds since signal: ");
  Serial.print(millisSinceSignal);
  Serial.print(", Adjusted seconds: ");
  Serial.print(seconds);
  Serial.print(", Milliseconds: ");
  Serial.println(milliseconds);
}

void updateDisplayFromRTC() {
  bool h12Flag, pmFlag;
  int hour = myRTC.getHour(h12Flag, pmFlag);
  int minute = myRTC.getMinute();
  int second = myRTC.getSecond();

  // Display only the ones digit of seconds
  nixieDrive(second % 10);

  // Print time to serial for debugging
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", hour, minute, second);
  Serial.println(timeStr);
}

void nixieDrive(int tubeValue) {
  // Set the segment pins to display the number
  for (int j = 0; j < 4; j++) {
    // Determine whether the current segment should be on or off based on the number
    boolean segmentState = segments[tubeValue][j];
    digitalWrite(segmentPins[j], segmentState);
  }
}

void printDateTime() {
  Serial.print(F("MSF Date: "));
  Serial.print(msf.m_iDay);
  Serial.print(F("/"));
  Serial.print(msf.m_iMonth);
  Serial.print(F("/"));
  Serial.print(2000 + msf.m_iYear); // Assuming 21st century
  Serial.print(F(" Time: "));
  if (msf.m_iHour < 10) Serial.print('0');
  Serial.print(msf.m_iHour);
  Serial.print(F(":"));
  if (msf.m_iMinute < 10) Serial.print('0');
  Serial.print(msf.m_iMinute);
  Serial.print(F(" DOW: "));
  Serial.println(msf.m_iDOW);
}

void printRTCTime() {
  bool h12Flag, pmFlag;
  int hour = myRTC.getHour(h12Flag, pmFlag);
  int minute = myRTC.getMinute();
  int second = myRTC.getSecond();
  
  Serial.print("RTC Time: ");
  if (hour < 10) Serial.print('0');
  Serial.print(hour);
  Serial.print(':');
  if (minute < 10) Serial.print('0');
  Serial.print(minute);
  Serial.print(':');
  if (second < 10) Serial.print('0');
  Serial.println(second);
}
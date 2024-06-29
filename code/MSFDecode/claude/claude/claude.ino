#include "MSFDecoder.h"

MSFDecoder msf;

// Debug LED pin
const int debugLedPin = 13;

// Variables for time keeping
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 60000; // Update every minute

void setup() {
  Serial.begin(115200);
  Serial.println(F("MSF Decoder started"));

  pinMode(debugLedPin, OUTPUT);

  msf.init(); // Initialize the MSF decoder
}

void loop() {
  unsigned long currentMillis = millis();

  // Blink debug LED to show the program is running
  digitalWrite(debugLedPin, (currentMillis / 500) % 2);

  // Check if we have a valid time and it's time to update
  if (msf.m_bHasValidTime && (currentMillis - lastUpdateTime >= updateInterval)) {
    printDateTime();
    lastUpdateTime = currentMillis;
  }

  // Print debug information every 5 seconds
  if (currentMillis % 5000 == 0) {
    printDebugInfo();
  }
}

void printDateTime() {
  Serial.print(F("Date: "));
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

void printDebugInfo() {
  Serial.print(F("Bit count: "));
  Serial.print(msf.getBitCount());
  Serial.print(F(" Has carrier: "));
  Serial.print(msf.getHasCarrier() ? "Yes" : "No");
  Serial.print(F(" Valid time: "));
  Serial.println(msf.m_bHasValidTime ? "Yes" : "No");
}
# MSF radio controlled Nixie clock project

The firmware is written, and it compiles clean for your exact chip. Here's everything you need to know.

What I built
New sketch in code/nixieClock/nixieClock/ — three files:

nixieClock.ino — the clock: RTC-driven display, MSF sync policy, anti-poisoning, watchdog, and serial diagnostics. All configuration (pin mapping, MSF pin/polarity, sync thresholds) sits in one marked block at the top.
MSFDecoder.h / MSFDecoder.cpp — a reworked decoder (Jarkman's proven pulse logic kept). Crucially, it now uses a pin-change interrupt, because your new board wires the receiver to PC3/A3, which has no INT0/INT1 — the old library literally could not have worked on this PCB. It also hands fixes to the main loop atomically and validates every field.
Your two questions
Libraries — yes, keep them. It uses exactly what you already had: Wire (built-in) and DS3231 by Andrew Wickert (v1.2.0). The MSF decoder is your own vendored source, improved. Nothing new to install except the board package.

Memory — plenty. I compiled it for real (portable arduino-cli + MiniCore in my scratchpad, nothing installed on your system): 12,824 bytes flash (39%) and 521 bytes RAM (25%) with serial diagnostics on; 9,494 / 342 bytes with serial off. Zero compiler warnings. Roughly 60% of the chip is still free.

One flashing caveat: your BOM says ATmega328PB, not 328P. Flash via MiniCore (board: ATmega328, variant: 328PB, external 16 MHz — your BOM's crystal is 16 MHz). Plain "Uno" settings will refuse the PB's device signature.

Failure cases handled
No signal / mixed signal: a fix is only trusted after fixed-bit checks, all four parity groups, calendar sanity (including days-in-month), and two consecutive frames exactly one minute apart. A corrupted frame essentially cannot pass twice consistently. Meanwhile the DS3231 keeps time — the display never depends on reception.
Summer/winter: MSF broadcasts UK civil time with BST already applied, so changeovers arrive over the air and the clock corrects itself within ~3 minutes of good reception after the switch.
RTC lost power (no battery / first boot): the DS3231's oscillator-stop flag is checked; until the first radio sync the display blinks to say "not set" instead of showing a confident wrong time.
RTC or I²C dies at runtime: the clock freewheels on the MCU crystal (~2 s/day worst case), the bus gets a timeout plus periodic re-init, and a 4-second watchdog reboots out of any hard hang.
Nixie poisoning: hourly slot-machine cycle at xx:59:45 (~13 s, every cathode), a power-on 0–9 sweep, and a 65-minute fallback cycle while unsynced.
Accuracy: the RTC is set on a whole-second boundary of the radio epoch (a few tens of ms error), and only rewritten when it drifts ≥2 s or once per day — no needless jitter.
Bring-up (in the file header too)
Flash with ENABLE_SERIAL 1, open the monitor at 115200, send h for the command list.
Send t — each tube steps 0–9 with commentary. The TUBE_PINS[][] table is my best guess; the PDFs don't reveal the U4→U1 header order, so fix the table until tubes match the commentary.
Send m — live MSF pulse widths. You should see ~100/200/300 ms off-pulses each second and 500 ms at the minute. If not: try MSF_PIN A2 (the schematic is ambiguous between PC2/PC3), or flip MSF_PULSE_ACTIVE_HIGH.
Send i anytime for full status (signal, frames accepted/rejected, last sync age).
Two hardware flags from the schematics
I couldn't find I²C pull-up resistors on the signal board's SDA/SCL (R1/R2 are LED resistors). The AVR's weak internal pull-ups often limp along, but if the RTC reads flaky, add ~4.7 kΩ to 5 V on both lines. The code tolerates and reports a bad bus either way.
The 16-pin tube header carries only signals — make sure the two boards share a ground through the single-pin headers, or the K155ID1s will see garbage levels.
Also note: if your real tube mapping turns out to use pins 0/1 (the schematic suggests PD0/PD1 do go to the header), set ENABLE_SERIAL 0 for the final flash — the UART owns those pins while serial is active.

I went back over both schematics, the BOMs, and the firmware with a critical eye. Findings below, ordered by how likely they are to actually bite you.

Likely bring-up killers (fix these first)
1. The MSF indicator LED is loading the receiver output. On the signal board, R2 (1 kΩ) + LED hang directly on the MSF net between the Canaduino /OUT and the MCU. The MAS6180C receiver IC inside that module can only source/sink about ±0.5 mA; an LED through 1 kΩ tries to pull ~3 mA, so the line will likely clamp around the LED's forward voltage (~2 V) — below the ATmega's VIH of ~3 V at 5 V. Result: the MCU may never see a HIGH and the decoder stays silent, exactly the "no signal" failure that's hardest to diagnose. Change: remove R2/LED from the signal line. Either buffer it with a small MOSFET/transistor, or (simplest) move the LED to a spare MCU pin and let the firmware mirror the MSF input onto it — a three-line code addition I can make once the pin exists.

2. No USB-C CC pull-downs on either board. Neither BOM contains 5.1 kΩ resistors, and both Type-C connectors show CC1/CC2 without terminations. Without a 5.1 kΩ pull-down on each CC pin, a USB-C source (C-to-C cable, laptop, modern charger) will never enable VBUS — the board only powers from legacy A-to-C cables. Add: 2× 5.1 kΩ (CC1, CC2) on the combined board.

3. No 100 nF decoupling on the signal board. The signal-stage BOM has only bulk capacitance (47 µF + 10 µF) — no ceramics at the ATmega's VCC/AVCC pins or at the DS3231. Sitting next to a 160 V flyback, that's an invitation for random resets and I²C corruption. Add: 100 nF at every supply pin (MCU ×2, RTC, receiver).

4. No programming or serial access. I can't find an ISP header, a UART header, a reset pull-up, or an auto-reset circuit. MOSI/MISO/SCK and RXD/TXD all appear to run off to the tube header (U4). You can technically program through U4 plus the RST pin, but it's fragile. Add on the combined board: a 6-pin ISP header, a 10 kΩ pull-up on RESET (the internal one is weak for an environment with an HV switcher), and ideally a UART header with the DTR/100 nF auto-reset — the firmware's diagnostic commands (t, m, i) are only useful if you can reach the UART. Related: route the tube BCD lines away from PD0/PD1 when you re-layout, so serial debugging never conflicts with the display (removing the whole ENABLE_SERIAL 0 caveat in the code).

5. Crystal discrepancy. The schematic footprint says "49S13.5M…" (13.5 MHz) but the BOM part is 16 MHz. Everything — millis(), the MSF pulse classification, serial baud — assumes 16 MHz. Verify what's actually soldered; the m monitor makes it obvious (100 ms pulses would read ~118 ms with a 13.5 MHz part).

Schematic improvements for the combined board
Keep the receiver far from the flyback. A 60 kHz AM receiver next to an LT3757 switcher and USB noise is the classic nixie-clock mistake. Put the ferrite antenna at the board edge farthest from the PSU (or on a short off-board lead), add an RC filter (e.g. 100 Ω + 100 µF) on the receiver's 5 V, and keep the tube BCD traces away from it.
Route the receiver's PDN pin to an MCU pin instead of tying it off. These receivers occasionally wedge their AGC; firmware could then power-cycle the receiver if no valid frame arrives for, say, 12 hours. Cheap insurance, and I can add the code for it.
I²C pull-ups and RTC battery. If "DS3231#2" is a socket for a ZS-042-style module, the module brings its own 4.7 kΩ pull-ups and a CR2032 — fine. If you move to a bare DS3231 chip on the combined board, you must add the pull-ups, VBAT with a coin cell, and a 100 nF. Without a battery, every power cut means a blinking display until the first radio fix (~3 min at best).
Optionally route DS3231's SQW to the MCU (with a pull-up — it's open-drain). A hardware 1 Hz tick is nicer than polling if you ever add a seconds display or a blinking colon.
Consider an INS-1 neon colon between tubes 2 and 3 (spare MCU pin + high-voltage transistor + ~220 kΩ from 160 V). Cosmetic, but it also gives a visible "alive" heartbeat.
Ground between the boards: the 16-pin tube header carries no GND, so the prototype pair depends entirely on the single-pin jumpers. Add several ground pins next to the signal lines (or just let the combined board solve it).
Flyback margins: the IRLR3110 (100 V) sees 5 V + ~16 V reflected + the leakage-inductance spike, and there's no snubber. The DA2032 has low leakage so it's probably fine, but scope the drain on first power-up; add an RC snubber footprint just in case. The feedback divider (1 M + pot) doubles as an HV bleeder, which is good. Add an HV warning on the silkscreen.
Two things that look wrong but are actually fine: the K155ID1's TTL inputs float HIGH when the MCU resets or the header is unplugged, which reads as BCD 15 → tubes blank during reset/programming — a happy accident worth keeping; and the 10 k anode resistors give ~2 mA per IN-12A, slightly conservative but adjustable via the pot.
Code improvements
Honest self-review of nixieClock.ino and the decoder:

Add the MSF activity LED mirror (one output pin, set in handleEdge or the display loop) once the hardware LED moves off the signal line — restores the blink-per-second feedback the board intended, without loading the receiver.
Add receiver power-cycling via PDN if you route it: "no confirmed fix in 12 h → pulse PDN" plus a counter in the status output.
RTC reads are 6 separate I²C transactions every 500 ms. Works, but a single burst read of registers 0x00–0x06 would be more atomic (no risk of reading minutes just as they roll over — currently a roll-over between getMinute() and getHour() could produce a one-off time that's wrong by an hour for half a second; the next 500 ms poll self-corrects, and the display only shows HH:MM, but a burst read removes the race entirely). This is the one genuine, if minor, correctness improvement I'd make in the current code.
applyFix blocks up to ~1 s spinning to the second boundary. Documented and harmless (sync happens at most daily), but if you ever add a seconds display it should become non-blocking.
Leap seconds are handled by rejection: a 61-bit minute fails the bitIndex == 60 check, the frame is discarded, and the next minute syncs normally. No change needed — just noting it's covered.
PULSE_OFF_OFFSET 20 is tuned for Jarkman's 2011 receiver, not the Canaduino. Use the m monitor on real hardware: if the off-widths cluster at, say, 95/195/295 you can drop it to 0. Worth re-checking rather than trusting the inherited constant.
Optional feature, since you skipped night blanking: duty-cycle dimming is nearly free with the K155ID1 (alternate digit/TUBE_BLANK at a few hundred Hz), which would allow night dimming and extend tube life without fully blanking.
The one thing I'd remove: nothing in the firmware, but on the hardware side the second USB-C connector disappears in the combined design, and R2+LED should come off the MSF line as described.

If you want, I can implement the firmware-side items now (burst RTC read, LED mirror pin, PDN control behind a #define so they're inert until the hardware exists) — say the word.
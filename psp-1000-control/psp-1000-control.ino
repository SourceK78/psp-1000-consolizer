/*******************************************************************************
 * This file is part of PsxNewLib.                                             *
 * Edited by SourceK for PSP-1000 Consolizer.                                  *
 *                                                                             *
 * Copyright (C) 2019-2020 by SukkoPera <software@sukkology.net>               *
 *                                                                             *
 * PsxNewLib is free software: you can redistribute it and/or                  *
 * modify it under the terms of the GNU General Public License as published by *
 * the Free Software Foundation, either version 3 of the License, or           *
 * (at your option) any later version.                                         *
 *                                                                             *
 * PsxNewLib is distributed in the hope that it will be useful,                *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 * GNU General Public License for more details.                                *
 *                                                                             *
 * You should have received a copy of the GNU General Public License           *
 * along with PsxNewLib. If not, see http://www.gnu.org/licenses.              *
 *******************************************************************************
 *
 * This sketch will dump to serial whatever is done on a PSX controller. It is
 * an excellent way to test that all buttons/sticks are read correctly.
 *
 * It's missing support for analog buttons, that will come in the future.
 *
 * This example drives the controller by bitbanging the protocol, there is
 * another similar one using the hardware SPI support.
 */

#include <DigitalIO.h>
#include <PsxControllerBitBang.h>
#include <Wire.h>
#include <SPI.h>

#include <pgmspace.h>

typedef const byte* PGM_BYTES_P;
#define PSTR_TO_F(s) reinterpret_cast<const __FlashStringHelper *> (s)

// Right analog stick center and deadzone settings.
// ANALOG_DEADZONE_MODE2 is for RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD
const byte ANALOG_CENTER   = 128;
const byte ANALOG_DEADZONE = 90;
const byte ANALOG_DEADZONE_MODE2 = 24;

// Debug logging
const bool DEBUG_LOG = false;

// Main loop pacing. Lower values reduce latency but poll the hardware more often.
const byte LOOP_DELAY_MS = 1;

// PSX controller (BitBang)
const byte PIN_PS2_DAT = 6;
const byte PIN_PS2_CMD = 7;
const byte PIN_PS2_ATT = 8;
const byte PIN_PS2_CLK = 9;

// I2C for MCP23017
const byte PIN_I2C_SDA = 13;
const byte PIN_I2C_SCL = 12;

// MCP23017
const uint8_t MCP23017_ADDR = 0x20;

// MCP23017 output bit for PSP HOME button (R3 position = bit 15)
// Adjust if your hardware uses a different output pin
const uint8_t HOME_BIT = 15;

// SPI for AD5142
const byte PIN_SPI_SCK  = 2;
const byte PIN_SPI_MOSI = 3;
const byte PIN_SPI_MISO = 4;
const byte PIN_SPI_SS   = 5;

// AD5142 Command (16-bit: [CMD5:CMD0=000001][A1:A0] | data)
const uint8_t CMD_RDAC1 = 0x10;  // 00000100: WRITE RDAC, address=0
const uint8_t CMD_RDAC2 = 0x11;  // 00000101: WRITE RDAC, address=1

// POWER LED (input) and RGB LED (PWM output)
const byte POWER_LED = 1;
const byte LED_R     = 39;
const byte LED_G     = 40;
const byte LED_B     = 41;

// Video scale mode select
const byte VIDEO_SCALE = 14;

int powerState = -1;

const char buttonSelectName[] PROGMEM = "Select";
const char buttonL3Name[] PROGMEM = "L3";
const char buttonR3Name[] PROGMEM = "R3";
const char buttonStartName[] PROGMEM = "Start";
const char buttonUpName[] PROGMEM = "Up";
const char buttonRightName[] PROGMEM = "Right";
const char buttonDownName[] PROGMEM = "Down";
const char buttonLeftName[] PROGMEM = "Left";
const char buttonL2Name[] PROGMEM = "L2";
const char buttonR2Name[] PROGMEM = "R2";
const char buttonL1Name[] PROGMEM = "L1";
const char buttonR1Name[] PROGMEM = "R1";
const char buttonTriangleName[] PROGMEM = "Triangle";
const char buttonCircleName[] PROGMEM = "Circle";
const char buttonCrossName[] PROGMEM = "Cross";
const char buttonSquareName[] PROGMEM = "Square";

const char* const psxButtonNames[PSX_BUTTONS_NO] PROGMEM = {
	buttonSelectName,
	buttonL3Name,
	buttonR3Name,
	buttonStartName,
	buttonUpName,
	buttonRightName,
	buttonDownName,
	buttonLeftName,
	buttonL2Name,
	buttonR2Name,
	buttonL1Name,
	buttonR1Name,
	buttonTriangleName,
	buttonCircleName,
	buttonCrossName,
	buttonSquareName
};

const char ctrlTypeUnknown[] PROGMEM = "Unknown";
const char ctrlTypeDualShock[] PROGMEM = "Dual Shock";
const char ctrlTypeDsWireless[] PROGMEM = "Dual Shock Wireless";
const char ctrlTypeGuitHero[] PROGMEM = "Guitar Hero";
const char ctrlTypeOutOfBounds[] PROGMEM = "(Out of bounds)";

const char* const controllerTypeStrings[PSCTRL_MAX + 1] PROGMEM = {
	ctrlTypeUnknown,
	ctrlTypeDualShock,
	ctrlTypeDsWireless,
	ctrlTypeGuitHero,
	ctrlTypeOutOfBounds
};

PsxControllerBitBang<PIN_PS2_ATT, PIN_PS2_CMD, PIN_PS2_DAT, PIN_PS2_CLK> psx;

boolean haveController = false;

// START+R3 selectable controller modes.
// The name is kept for compatibility, but some modes also affect D-Pad/left analog.
enum RightAnalogMode : uint8_t {
	RIGHT_ANALOG_MODE_OFF = 0,
	RIGHT_ANALOG_MODE_DPAD = 1,
	RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD = 2,
	RIGHT_ANALOG_MODE_STAR_SOLDIER = 3
};

RightAnalogMode rightAnalogMode = RIGHT_ANALOG_MODE_OFF;
bool popnMusicControllerMode = false;

void updateModeLed() {
	// !powerState: mode-based color
	// powerState : red fixed
	if (!powerState) {
		// OFF: blue
		// RIGHT_ANALOG_MODE_DPAD: green,
		// RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD: magenta
		// RIGHT_ANALOG_MODE_STAR_SOLDIER: cyan
		// Pop'n Music: white
		if (popnMusicControllerMode) {
			analogWrite(LED_R, 64);
			analogWrite(LED_G, 64);
			analogWrite(LED_B, 64);
		} else if (rightAnalogMode == RIGHT_ANALOG_MODE_DPAD) {
			analogWrite(LED_R, 0);
			analogWrite(LED_G, 64);
			analogWrite(LED_B, 0);
		} else if (rightAnalogMode == RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD) {
			analogWrite(LED_R, 64);
			analogWrite(LED_G, 0);
			analogWrite(LED_B, 64);
		} else if (rightAnalogMode == RIGHT_ANALOG_MODE_STAR_SOLDIER) {
			analogWrite(LED_R, 0);
			analogWrite(LED_G, 64);
			analogWrite(LED_B, 64);
		} else {
			analogWrite(LED_R, 0);
			analogWrite(LED_G, 0);
			analogWrite(LED_B, 64);
		}
	} else {
		analogWrite(LED_R, 64);
		analogWrite(LED_G, 0);
		analogWrite(LED_B, 0);
	}
}



void writeButtons(uint16_t value) {
	Wire.beginTransmission(MCP23017_ADDR);
	Wire.write(0x14);  // OLATA
	Wire.write(value & 0xFF);        // Lower 8bit
	Wire.write((value >> 8) & 0xFF); // Upper 8bit
	Wire.endTransmission();
}

// Remap PSX button word to desired output bit order for MCP23017:
// out bit 0:Right, 1:Down, 2:Up, 3:Left, 4:L1, 5:Triangle, 6:Circle, 7:R1,
//         8:Square, 9:Cross, 10:Start, 11:Select, 12:L3, 13:R2, 14:L2, 15:R3
uint16_t remapButtons(uint16_t b) {
	uint16_t out = 0;
	if (b & (1 <<  5)) out |= (1 <<  0); // Right
	if (b & (1 <<  6)) out |= (1 <<  1); // Down
	if (b & (1 <<  4)) out |= (1 <<  2); // Up
	if (b & (1 <<  7)) out |= (1 <<  3); // Left
	if (b & (1 << 10)) out |= (1 <<  4); // L1
	if (b & (1 << 12)) out |= (1 <<  5); // Triangle
	if (b & (1 << 13)) out |= (1 <<  6); // Circle
	if (b & (1 << 11)) out |= (1 <<  7); // R1
	if (b & (1 << 15)) out |= (1 <<  8); // Square
	if (b & (1 << 14)) out |= (1 <<  9); // Cross
	if (b & (1 <<  3)) out |= (1 << 10); // Start
	if (b & (1 <<  0)) out |= (1 << 11); // Select
	if (b & (1 <<  1)) out |= (1 << 12); // L3
	if (b & (1 <<  9)) out |= (1 << 13); // R2
	if (b & (1 <<  8)) out |= (1 << 14); // L2
	if (b & (1 <<  2)) out |= (1 << 15); // R3
	return out;
}

// Rotate D-Pad 90 degrees counterclockwise for PSP Star Soldier vertical play:
// PS2 Up -> PSP Right, Down -> Left, Left -> Up, Right -> Down.
PsxButtons rotateStarSoldierDpad(PsxButtons b) {
	PsxButtons out = b & ~((1 << 4) | (1 << 5) | (1 << 6) | (1 << 7));
	if (b & (1 << 4)) out |= (1 << 5); // Up -> Right
	if (b & (1 << 6)) out |= (1 << 7); // Down -> Left
	if (b & (1 << 7)) out |= (1 << 4); // Left -> Up
	if (b & (1 << 5)) out |= (1 << 6); // Right -> Down
	return out;
}

// Pop'n Music controller remap for PSP:
// PSX Up -> PSP Right, Circle -> Left, Cross -> Up, Square -> Down,
// Triangle -> Triangle, R1 -> L, L1 -> Circle, R2 -> R, L2 -> Cross,
// Start -> Start, Select -> Select, Select+L1 -> Square.
// PSX Down/Left/Right are ignored.
uint16_t remapPopnMusicButtons(uint16_t b) {
	uint16_t out = 0;
	bool selectL1Combo = (b & (1 << 0)) && (b & (1 << 10));
	if (b & (1 <<  4)) out |= (1 <<  0); // Up -> Right
	if (b & (1 << 13)) out |= (1 <<  3); // Circle -> Left
	if (b & (1 << 14)) out |= (1 <<  2); // Cross -> Up
	if (b & (1 << 15)) out |= (1 <<  1); // Square -> Down
	if (b & (1 << 12)) out |= (1 <<  5); // Triangle -> Triangle
	if (b & (1 << 11)) out |= (1 <<  4); // R1 -> L
	if (selectL1Combo) out |= (1 <<  8); // Select+L1 -> Square
	else if (b & (1 << 10)) out |= (1 <<  6); // L1 -> Circle
	if (b & (1 <<  9)) out |= (1 <<  7); // R2 -> R
	if (b & (1 <<  8)) out |= (1 <<  9); // L2 -> Cross
	if (b & (1 <<  3)) out |= (1 << 10); // Start -> Start
	if (!selectL1Combo && (b & (1 <<  0))) out |= (1 << 11); // Select -> Select
	return out;
}

void writePot(uint8_t pot, uint8_t value) {
	uint8_t command = (pot == 0) ? CMD_RDAC1 : CMD_RDAC2;

	SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));
	digitalWrite(PIN_SPI_SS, LOW);
	SPI.transfer(command);
	SPI.transfer(map(value, 0, 255, 255, 0)); // Inverse Value
	digitalWrite(PIN_SPI_SS, HIGH);
	SPI.endTransaction();
}

byte psxButtonToIndex (PsxButtons psxButtons) {
	byte i;

	for (i = 0; i < PSX_BUTTONS_NO; ++i) {
		if (psxButtons & 0x01) {
			break;
		}

		psxButtons >>= 1U;
	}

	return i;
}

void dumpButtons (PsxButtons psxButtons) {
	if (!DEBUG_LOG) {
		return;
	}

	static PsxButtons lastB = 0;

	if (psxButtons != lastB) {
		lastB = psxButtons;     // Save it before we alter it

		Serial.print (F("Pressed: "));

		for (byte i = 0; i < PSX_BUTTONS_NO; ++i) {
			byte b = psxButtonToIndex (psxButtons);
			if (b < PSX_BUTTONS_NO) {
				PGM_BYTES_P bName = reinterpret_cast<PGM_BYTES_P> (pgm_read_ptr (&(psxButtonNames[b])));
				Serial.print (PSTR_TO_F (bName));
			}

			psxButtons &= ~(1 << b);

			if (psxButtons != 0) {
				Serial.print (F(", "));
			}
		}

		Serial.println ();
	}
}

void dumpAnalog (const char *str, const byte x, const byte y) {
	if (!DEBUG_LOG) {
		return;
	}

	Serial.print (str);
	Serial.print (F(" analog: x = "));
	Serial.print (x);
	Serial.print (F(", y = "));
	Serial.println (y);
}

void setup () {
	delay (300);
	if (DEBUG_LOG) {
		Serial.begin (115200);
		Serial.println (F("Ready!"));
	}

	Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
	Wire.setClock(400000);

	pinMode(PIN_SPI_SS, OUTPUT);
	digitalWrite(PIN_SPI_SS, HIGH);  // CS Initial HIGH
	SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_SS);

	pinMode(POWER_LED, INPUT_PULLUP);
	pinMode(LED_R, OUTPUT);
	pinMode(LED_G, OUTPUT);
	pinMode(LED_B, OUTPUT);

	pinMode(VIDEO_SCALE, OUTPUT);
	digitalWrite(VIDEO_SCALE, LOW);  // Mode Select Initial LOW

	// // Set OLATA to full Low (0x00) Just in case...
	// Wire.beginTransmission(MCP23017_ADDR);
	// Wire.write(0x14);   // IODIRA
	// Wire.write(0x00);   // All pin output
	// Wire.endTransmission();

	// // Set OLATB to full Low (0x00) Just in case...
	// Wire.beginTransmission(MCP23017_ADDR);
	// Wire.write(0x15);   // IODIRB
	// Wire.write(0x00);   // All pin output
	// Wire.endTransmission();
	
	// Set IODIRA to full output (0x00)
	Wire.beginTransmission(MCP23017_ADDR);
	Wire.write(0x00);   // IODIRA
	Wire.write(0x00);   // All pin output
	Wire.endTransmission();

	// Set IODIRB to full output (0x00)
	Wire.beginTransmission(MCP23017_ADDR);
	Wire.write(0x01);   // IODIRB
	Wire.write(0x00);   // All pin output
	Wire.endTransmission();

	// Initialize
	writeButtons(0xFFFF);
	writePot(0, ANALOG_CENTER);
	writePot(1, ANALOG_CENTER);

	updateModeLed();
}

void loop () {
	static byte slx, sly, srx, sry;
	static bool forceLeftAnalogWrite = true;

	if (!haveController) {
		if (psx.begin ()) {
			if (DEBUG_LOG) {
				Serial.println (F("Controller found!"));
			}
			delay (300);
			if (!psx.enterConfigMode ()) {
				if (DEBUG_LOG) {
					Serial.println (F("Cannot enter config mode"));
				}
			} else {
				PsxControllerType ctype = psx.getControllerType ();
				PGM_BYTES_P cname = reinterpret_cast<PGM_BYTES_P> (pgm_read_ptr (&(controllerTypeStrings[ctype < PSCTRL_MAX ? static_cast<byte> (ctype) : PSCTRL_MAX])));
				if (DEBUG_LOG) {
					Serial.print (F("Controller Type is: "));
					Serial.println (PSTR_TO_F (cname));
				}

				if (!psx.enableAnalogSticks ()) {
					if (DEBUG_LOG) {
						Serial.println (F("Cannot enable analog sticks"));
					}
				}

				if (!psx.enableAnalogButtons ()) {
					if (DEBUG_LOG) {
						Serial.println (F("Cannot enable analog buttons"));
					}
				}

				if (!psx.enableRumble (false)) {
					if (DEBUG_LOG) {
						Serial.println (F("Cannot disable rumble"));
					}
				}

				if (!psx.exitConfigMode ()) {
					if (DEBUG_LOG) {
						Serial.println (F("Cannot exit config mode"));
					}
				}
			}

			haveController = true;
			popnMusicControllerMode = false;
			rightAnalogMode = RIGHT_ANALOG_MODE_OFF;
			updateModeLed();
		}
	} else {
		if (!psx.read ()) {
			if (DEBUG_LOG) {
				Serial.println (F("Controller lost :("));
			}
			haveController = false;
			popnMusicControllerMode = false;
			rightAnalogMode = RIGHT_ANALOG_MODE_OFF;
			updateModeLed();
			writeButtons(0xFFFF);
			writePot(0, ANALOG_CENTER);
			writePot(1, ANALOG_CENTER);
			digitalWrite(VIDEO_SCALE, LOW);
			slx = sly = srx = sry = 255;  // Force analog re-report on reconnect
			forceLeftAnalogWrite = true;
		} else {
			uint8_t rx, ry;
			psx.getRightAnalog (rx, ry);
			if (rx != srx || ry != sry) {
				dumpAnalog ("Right", rx, ry);
				srx = rx;
				sry = ry;
			}

			PsxButtons rawButtons = psx.getButtonWord ();
			bool btnL2    = rawButtons & (1 << 8);  // L2
			bool btnR2    = rawButtons & (1 << 9);  // R2
			bool btnR3    = rawButtons & (1 << 2);  // R3
			bool btnStart = rawButtons & (1 << 3);  // Start
			bool btnRight = rawButtons & (1 << 5);  // Right
			bool btnDown  = rawButtons & (1 << 6);  // Down
			bool btnLeft  = rawButtons & (1 << 7);  // Left
			bool popnMusicControllerDetected = btnLeft && btnRight && btnDown;
			bool homePress         = !popnMusicControllerMode && !popnMusicControllerDetected && btnStart && btnRight;  // START+RIGHT → HOME
			bool modeToggleCombo   = !popnMusicControllerMode && !popnMusicControllerDetected && btnStart && btnR3;     // START+R3 → mode toggle

			if (popnMusicControllerDetected && !popnMusicControllerMode) {
				popnMusicControllerMode = true;
				updateModeLed();
				if (DEBUG_LOG) {
					Serial.println (F("Pop'n Music controller mode enabled"));
				}
			} else if (!popnMusicControllerDetected && popnMusicControllerMode) {
				popnMusicControllerMode = false;
				rightAnalogMode = RIGHT_ANALOG_MODE_OFF;
				updateModeLed();
				if (DEBUG_LOG) {
					Serial.println (F("Pop'n Music controller mode disabled"));
				}
			}

			// Toggle RightAnalogMode on rising edge of START+R3:
			// OFF -> RIGHT_ANALOG_MODE_DPAD -> RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD
			//     -> RIGHT_ANALOG_MODE_STAR_SOLDIER -> OFF
			static bool lastModeToggle = false;
			if (!powerState && modeToggleCombo && !lastModeToggle) {
				rightAnalogMode = static_cast<RightAnalogMode>((rightAnalogMode + 1) % 4);
				updateModeLed();
			}
			lastModeToggle = modeToggleCombo;

			// Build filtered buttons for MCP23017:
			// L2/R2 are removed from default pass-through (handled specially below)
			PsxButtons filteredButtons = popnMusicControllerMode ? rawButtons : rawButtons & ~((1 << 8) | (1 << 9));

			// Remove Start when used as modifier key (HOME, VOL+, VOL-, mode toggle)
			if (homePress || modeToggleCombo || (!popnMusicControllerMode && btnStart && (btnL2 || btnR2))) {
				filteredButtons &= ~(1 << 3);
			}

			// START+RIGHT → HOME: also suppress Right button
			if (homePress) {
				filteredButtons &= ~(1 << 5);
			}

			// START+R3 → mode toggle: also suppress R3 button
			if (modeToggleCombo) {
				filteredButtons &= ~(1 << 2);
			}

			// START+L2 → VOL- (restore L2 bit),  START+R2 → VOL+ (restore R2 bit)
			if (!popnMusicControllerMode && btnStart && btnL2) filteredButtons |= (1 << 8);
			if (!popnMusicControllerMode && btnStart && btnR2) filteredButtons |= (1 << 9);

			// Apply right analog remap modes.
			// RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD uses the smaller enum-value-2 deadzone.
			byte rightAnalogDeadzone = (rightAnalogMode == RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD) ? ANALOG_DEADZONE_MODE2 : ANALOG_DEADZONE;
			// D-Pad bits: UP=4 RIGHT=5 DOWN=6 LEFT=7
			PsxButtons rightToDpadBits = 0;
			if (ry < ANALOG_CENTER - rightAnalogDeadzone) rightToDpadBits |= (1 << 4); // UP
			if (rx > ANALOG_CENTER + rightAnalogDeadzone) rightToDpadBits |= (1 << 5); // RIGHT
			if (ry > ANALOG_CENTER + rightAnalogDeadzone) rightToDpadBits |= (1 << 6); // DOWN
			if (rx < ANALOG_CENTER - rightAnalogDeadzone) rightToDpadBits |= (1 << 7); // LEFT

			// Face bits : TRI=12 CIRCLE=13 CROSS=14 SQUARE=15
			if (!popnMusicControllerMode && rightAnalogMode == RIGHT_ANALOG_MODE_DPAD) {
				filteredButtons |= rightToDpadBits;
			} else if (!popnMusicControllerMode && rightAnalogMode == RIGHT_ANALOG_MODE_FACE_AND_FACE_TO_DPAD) {
				// Keep current physical D-Pad state, then add face->D-Pad mapping.
				PsxButtons physicalDpadBits = filteredButtons & ((1 << 4) | (1 << 5) | (1 << 6) | (1 << 7));

				// Move physical face buttons to D-Pad
				bool faceTriangle = filteredButtons & (1 << 12);
				bool faceCircle   = filteredButtons & (1 << 13);
				bool faceCross    = filteredButtons & (1 << 14);
				bool faceSquare   = filteredButtons & (1 << 15);

				filteredButtons &= ~((1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) |
				                     (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15));

				// Physical D-Pad remains active
				filteredButtons |= physicalDpadBits;

				// Face buttons also work as D-Pad
				if (faceTriangle) filteredButtons |= (1 << 4); // TRI -> UP
				if (faceCircle)   filteredButtons |= (1 << 5); // CIRCLE -> RIGHT
				if (faceCross)    filteredButtons |= (1 << 6); // CROSS -> DOWN
				if (faceSquare)   filteredButtons |= (1 << 7); // SQUARE -> LEFT

				// Move right analog stick directions to face buttons
				if (rightToDpadBits & (1 << 4)) filteredButtons |= (1 << 12); // UP -> TRI
				if (rightToDpadBits & (1 << 5)) filteredButtons |= (1 << 13); // RIGHT -> CIRCLE
				if (rightToDpadBits & (1 << 6)) filteredButtons |= (1 << 14); // DOWN -> CROSS
				if (rightToDpadBits & (1 << 7)) filteredButtons |= (1 << 15); // LEFT -> SQUARE
			}
			PsxButtons outputButtons = filteredButtons;
			if (!popnMusicControllerMode && rightAnalogMode == RIGHT_ANALOG_MODE_STAR_SOLDIER) {
				outputButtons = rotateStarSoldierDpad(outputButtons);
			}
			dumpButtons (outputButtons);

			// Write to MCP23017: apply HOME button on HOME_BIT (active low)
			uint16_t mappedButtons = popnMusicControllerMode ? remapPopnMusicButtons (outputButtons) : remapButtons (outputButtons);
			uint16_t mcpOutput = ~mappedButtons;
			if (homePress) {
				mcpOutput &= ~(1 << HOME_BIT);
			}
			writeButtons (mcpOutput);

			// VIDEO_SCALE: HIGH only when both Start (bit 3) and Left (bit 7) are pressed
			digitalWrite(VIDEO_SCALE, (!popnMusicControllerMode && (filteredButtons & (1 << 3)) && (filteredButtons & (1 << 7))) ? HIGH : LOW);

			// Left analog stick:
			// L2       → -X (left),   Y = center   (when Start not held)
			// R2       → +X (right),  Y = center   (when Start not held)
			// L2+R2    → -Y (up),     X = center   (when Start not held)
			// START+L2/R2 → VOL (handled above), use physical analog
			uint8_t lx, ly;
			psx.getLeftAnalog (lx, ly);

			uint8_t outLx, outLy;
			if (popnMusicControllerMode) {
				outLx = ANALOG_CENTER;
				outLy = ANALOG_CENTER;
			} else if (!btnStart && btnL2 && btnR2) {
				outLx = ANALOG_CENTER;
				outLy = 0;            // -Y (push up)
			} else if (!btnStart && btnL2) {
				outLx = 0;              // -X (push left)
				outLy = ANALOG_CENTER;
			} else if (!btnStart && btnR2) {
				outLx = 255;            // +X (push right)
				outLy = ANALOG_CENTER;
			} else {
				outLx = lx;
				outLy = ly;
			}

			if (!popnMusicControllerMode && rightAnalogMode == RIGHT_ANALOG_MODE_STAR_SOLDIER) {
				uint8_t rotatedLx = constrain(ANALOG_CENTER + ANALOG_CENTER - outLy, 0, 255);
				uint8_t rotatedLy = outLx;
				outLx = rotatedLx;
				outLy = rotatedLy;
			}

			if (forceLeftAnalogWrite || outLx != slx || outLy != sly) {
				dumpAnalog ("Left", outLx, outLy);
				writePot(0, outLx);
				writePot(1, outLy);
				slx = outLx;
				sly = outLy;
				forceLeftAnalogWrite = false;
			}
		}
	}

	// Get power state
	int curPwr = digitalRead(POWER_LED);
	if ( powerState != curPwr ) {
		powerState = curPwr;
		updateModeLed();

		if (DEBUG_LOG) {
			Serial.print (F("Power State: "));
			Serial.println (curPwr);
		}
	}

	delay (LOOP_DELAY_MS);
}

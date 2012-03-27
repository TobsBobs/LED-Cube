/*
 * main.c
 *
 * Copyright 2011 Thomas Buck <xythobuz@me.com>
 * Copyright 2011 Max Nuding <max.nuding@gmail.com>
 * Copyright 2011 Felix Bäder <baeder.felix@gmail.com>
 *
 * This file is part of LED-Cube.
 *
 * LED-Cube is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LED-Cube is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LED-Cube.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef F_CPU
#define F_CPU 16000000L
#endif
 
#define OK 0x42
#define ERROR 0x23

#ifdef DEBUG
#define VERSION "v2 (Debug Build)\nNOT COMPATIBLE WITH CubeControl!\n"
#else
#define VERSION "v2 Release\n"
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <stdlib.h>

#include "serial.h"
#include "cube.h"
#include "time.h"
#include "audio.h"
#include "mem.h"
#include "memLayer.h"
#include "twi.h"

#define NOERROR 0
// Audio does not answer
#define AUDIOERROR 1
// Memory does not answer
#define MEMORYERROR 2
// Memory not writeable
#define MEMORYWRITEERROR 4
// x = errorcode, e = error definition, not NOERROR
#define ISERROR(x, e) ((x) & (e))

void serialHandler(char c);
void sendAudioData(void);
void recieveAnimations(void);
void transmitAnimations(void);
uint8_t audioModeSelected(void);
void setPixelBuffer(uint8_t x, uint8_t y, uint8_t z, uint8_t *buf);
void setRow(uint8_t x, uint8_t z, uint8_t height, uint8_t *buf);
void visualizeAudioData(uint8_t *audioData, uint8_t *imageData);
#ifdef DEBUG
void printErrors(uint8_t e);
uint8_t selfTest(void);

#include "snake.c"
#endif

uint8_t refreshAnimationCount = 1;
uint8_t lastButtonState = 0;
char buffer[11];

uint8_t defaultImage[64] = 	{	0xe7, 0xc3, 0xa5, 0x18, 0x18, 0xa5, 0xc3, 0xe7,
								0x81, 0x81, 0x00, 0x18, 0x18, 0x00, 0x81, 0x81,
								0x81, 0x00, 0x81, 0x18, 0x18, 0x81, 0x00, 0x81,
								0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00,
								0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00,
								0x81, 0x00, 0x81, 0x18, 0x18, 0x81, 0x00, 0x81,
								0x81, 0x81, 0x00, 0x18, 0x18, 0x00, 0x81, 0x81,
								0xe7, 0xc3, 0xa5, 0x18, 0x18, 0xa5, 0xc3, 0xe7 };

int main(void) {
	uint8_t *audioData = NULL;
	uint8_t *imageData = NULL;
	uint8_t i, length = 0, lastMode;
	uint16_t count;
	uint64_t lastChecked;

	initCube();
	serialInit(25, 8, NONE, 1);
	i2c_init();
	initSystemTimer();
	sei(); // Enable Interrupts

	DDRD = 0xFC; // Mosfets as Output
	DDRB = 0xFE;
	DDRC = 0xFF; // Latch Enable
	DDRA = 0xFF; // Latch Data

	setImage(defaultImage); // Display something

#ifdef DEBUG
	// Kill animation counter in debug mode
	// => Don't preserve animations while power down
	setAnimationCount(0);

	i = selfTest();
	if (i) {
		serialWriteString("Self-Test Error: 0b");
		serialWriteString(itoa(i, buffer, 2));
		serialWrite('\n');
		printErrors(i);
	}
#endif

#ifdef DEBUG
	serialWriteString("\n\nInitialized: ");
	serialWriteString(VERSION);
	serialWriteString("Took ");
	serialWriteString(itoa(getSystemTime(), buffer, 10));
	serialWriteString(" ms!\n");
#endif

	lastMode = audioModeSelected();
	lastChecked = getSystemTime();

	i = 0;
	count = getAnimationCount();
	while (1) {
		if(lastMode) {
			// Get Audio Data and visualize it
			if (isFinished()) {
				audioData = getAudioData();
				if (audioData != NULL) {
					imageData = (uint8_t *)malloc(64);
					visualizeAudioData(audioData, imageData);
					setImage(imageData);
					free(imageData);
					free(audioData);
				}
			}
		} else {
			if (refreshAnimationCount) {
				// Get animation count stored in FRAM via TWI, if needed
				count = getAnimationCount();
				refreshAnimationCount = 0;
				i = 0;
			}

			if (count > 0) {
				if (isFinished() > length) {
					// Load next image
					if (i < (count - 1)) {
						i++;
					} else {
						i = 0;
					}

					imageData = getFrame(i);
					length = imageData[64];
					setImage(imageData);
					free(imageData);
				}
			}
		}

		if (serialHasChar()) {
			serialHandler((char)(serialGet()));
		}

		if ((getSystemTime() - lastChecked) > 150) {
			lastMode = audioModeSelected();
			lastChecked = getSystemTime();
		} 
	}

	close();
	return 0;
}

#ifdef DEBUG
uint8_t selfTest(void) {
	uint8_t result = NOERROR;
	
	uint8_t *data = getAudioData();
	if (data == NULL) {
		result |= AUDIOERROR;
	} else {
		free(data);
	}

	data = memGetBytes(0, 1);
	if (data == NULL) {
		result |= MEMORYERROR;
	} else {
		free(data);
	}

	setGeneralPurposeByte(0, 0x42);
	if (getGeneralPurposeByte(0) != 0x42) {
		result |= MEMORYWRITEERROR;
	}

	return result;
}

void printErrors(uint8_t e) {
	if (ISERROR(e, AUDIOERROR)) {
		serialWriteString(" => No answer from Audio!\n");
	}
	if (ISERROR(e, MEMORYERROR)) {
		serialWriteString(" => No answer from Memory!\n");
	}
	if (ISERROR(e, MEMORYWRITEERROR)) {
		serialWriteString(" => Can't write to Memory!\n");
	}
}
#endif

void serialHandler(char c) {
	// Used letters:
	// a, c, d, g, s, t, v, x
#ifdef DEBUG
	serialWrite(c);
	serialWriteString(": ");
#endif

	switch(c) {
	case OK:
		serialWrite(OK);
		break;

	case 'h': case 'H':
		serialWriteString("(d)elete, (g)et anims, (s)et anims, (v)ersion\n");
#ifdef DEBUG
		serialWriteString("(t)ime, (a)udio, (c)ount, (x)Custom count\n");
		serialWriteString("(y)Set fixed animation count\n");
		serialWriteString("S(e)lf Test\n");
		serialWriteString("Play S(n)ake\n");
#endif
		break;

	case 'd': case 'D':
		clearMem();
		serialWrite(OK);
		break;

	case 'g': case 'G':
		transmitAnimations();
		break;

	case 's': case 'S':
		recieveAnimations();
		break;

	case 'v': case 'V':
		serialWriteString(VERSION);
		break;

#ifdef DEBUG
	case 't': case 'T':
		serialWriteString("System Time: ");
		serialWriteString(ltoa(getSystemTime(), buffer, 10));
		serialWriteString("ms");
		if (getSystemTime() > 1000) {
			serialWriteString(" (");
			serialWriteString(itoa(getSystemTime() / 1000, buffer, 10));
			itoa(getSystemTime() % 1000, buffer, 10);
			if (buffer[0] != '\0')
				serialWrite('.');
			if (buffer[2] == '\0') 
				serialWrite('0');
			if (buffer[1] == '\0')
				serialWrite('0');
			if (buffer[0] != '\0')
				serialWriteString(buffer);
			serialWriteString("s)\n");
		} else {
			serialWrite('\n');
		}
		break;

	case 'a': case 'A':
		sendAudioData();
		break;

	case 'c': case 'C':
		serialWriteString(itoa(getAnimationCount(), buffer, 10));
		serialWriteString(" Frames stored\n");
		break;

	case 'x': case 'X':
		// Get byte, store as animation count
		serialWriteString("Send a byte... ");
		while (!serialHasChar());
		c = serialGet();
		setAnimationCount(c);
		serialWriteString(itoa(c, buffer, 10));
		serialWriteString(" written!\n");
		break;

	case 'y': case 'Y':
		setAnimationCount(0x2201);
		serialWriteString("Animation count now 8705!\n");
		break;

	case 'e': case 'E':
		c = selfTest();
		serialWriteString("Self-Test: 0b");
		serialWriteString(itoa(c, buffer, 2));
		serialWrite('\n');
		printErrors(c);
		break;

	case 'n': case 'N':
		snake();
		break;
#endif

	default:
		serialWrite(ERROR);
		break;
	}
	// c was used as temp var and does not contain the char anymore...!
}

#ifdef DEBUG
void sendAudioData(void) {
	uint8_t i;
	uint8_t *audioData = getAudioData();
	if (audioData == NULL) {
		serialWriteString("Could not access device!\n");
	} else {
		serialWriteString("Audio Data:\n");
		for (i = 0; i < 7; i++) {
			serialWrite(i + '0');
			serialWriteString(": ");
			itoa(audioData[i], buffer, 10);
			serialWriteString(buffer);
			serialWrite('\n');
		}
		free(audioData);
	}
}
#endif

void recieveAnimations() {
	uint8_t animCount, a, frameCount, f, i;
	uint16_t completeCount = 0, character;
	uint8_t frame[65];

	serialWrite(OK); // We are ready...

	character = serialGet();
	while (character & 0xFF00) { // Wait for answer
		character = serialGet();
	}

	animCount = (uint8_t)(character & 0x00FF); // Got animation count
	serialWrite(OK);

	for (a = 0; a < animCount; a++) {
		character = serialGet();
		while (character & 0xFF00) { // Wait for answer
			character = serialGet();
		}

		frameCount = (uint8_t)(character & 0x00FF); // Got frame count
		serialWrite(OK);

		for (f = 0; f < frameCount; f++) {
			character = serialGet();
			while (character & 0xFF00) { // Wait for answer
				character = serialGet();
			}

			frame[64] = (uint8_t)(character & 0x00FF); // Got duration
			serialWrite(OK);

			for (i = 0; i < 64; i++) {
				character = serialGet();
				while (character & 0xFF00) { // Wait for answer
					character = serialGet();
				}

				frame[i] = (uint8_t)(character & 0x00FF); // Got data byte
			}
			serialWrite(OK);

			setFrame(completeCount++, frame);
		}
	}

	character = serialGet();
	while (character & 0xFF00) { // Wait for answer
		character = serialGet();
	}
	character = serialGet();
	while (character & 0xFF00) { // Wait for answer
		character = serialGet();
	}
	character = serialGet();
	while (character & 0xFF00) { // Wait for answer
		character = serialGet();
	}
	character = serialGet();
	while (character & 0xFF00) { // Wait for answer
		character = serialGet();
	}
	serialWrite(OK);
	setAnimationCount(completeCount);
	refreshAnimationCount = 1;
}

void transmitAnimations() {
	// We store no animation information in here
	// So we have to place all frames in one or more
	// animations... We need 8 animations max...
	uint8_t animationsToGo;
	uint16_t framesToGo = getAnimationCount();
	uint16_t character;
	uint8_t a;
	uint8_t f, fMax, i;
	uint8_t *frame;

	if ((framesToGo % 255) == 0) {
		animationsToGo = framesToGo / 255;
	} else {
		animationsToGo = (framesToGo / 255) + 1;
	}

	serialWrite(OK);
	serialWrite(animationsToGo);
	while ((character = serialGet()) & 0xFF00); // Wait for answer
	if ((character & 0x00FF) != OK) { // Error code recieved
		return;
	}

	for (a = 0; a < animationsToGo; a++) {
		if (framesToGo > 255) {
			fMax = 255;
		} else {
			fMax = framesToGo;
		}

		serialWrite(fMax); // Number of Frames in current animation
		while ((character = serialGet()) & 0xFF00); // Wait for answer
		if ((character & 0x00FF) != OK) { // Error code recieved
			return;
		}

		for (f = 0; f < fMax; f++) {
			frame = getFrame(f + (255 * a));

			serialWrite(frame[64]); // frame duration
			while ((character = serialGet()) & 0xFF00); // Wait for answer
			if ((character & 0x00FF) != OK) { // Error code recieved
				free(frame);
				return;
			}

			for (i = 0; i < 64; i++) {
				serialWrite(frame[i]);
			}
			while ((character = serialGet()) & 0xFF00); // Wait for answer
			if ((character & 0x00FF) != OK) { // Error code recieved
				free(frame);
				return;
			}

			free(frame);
		}
		framesToGo -= fMax;
	}

	serialWrite(OK);
	serialWrite(OK);
	serialWrite(OK);
	serialWrite(OK);

	while ((character = serialGet()) & 0xFF00); // Wait for answer
	// Error code ignored...
}

uint8_t audioModeSelected(void) {
	// Pushbutton: PB0, Low active

	if (!(PINB & (1 << PB0))) {
		// Button pushed
		if (lastButtonState == 0) {
			lastButtonState = 1;
		} else {
			lastButtonState = 0;
		}

#ifdef DEBUG
		serialWriteString("New State (");
		serialWriteString(itoa(lastButtonState, buffer, 10));
		serialWriteString(")\n");
#endif

	}
	return lastButtonState;
}

void setRow(uint8_t x, uint8_t z, uint8_t height, uint8_t *buf) {
	uint8_t i = 0;
	for (; i < height; i++) {
		setPixelBuffer(x, i, z, buf);
	}
}

void setPixelBuffer(uint8_t x, uint8_t y, uint8_t z, uint8_t *buf) {
	buf[(8 * z) + y] |= (1 << x);
}

void visualizeAudioData(uint8_t *audioData, uint8_t *imageData) {
	uint8_t i;
	for (i = 0; i < 64; i++) {
		imageData[i] = 0;
	}

	// 8 LEDs, Max Val 255:
	// 256 / 8 = 32 => Divide by 31 (FACTOR) to get num of leds to light
	// 255 / FACTOR = 8,...
	// 127 / FACTOR = 4,...

	#define FACTOR 31

	// Could not figure out a way to represent this easily in a loop
	// without using a shitload of 'if's...
	setRow(0, 0, (audioData[0] / FACTOR), imageData);
	setRow(0, 1, (audioData[0] / FACTOR), imageData);
	setRow(1, 0, (audioData[0] / FACTOR), imageData);

	setRow(0, 2, (audioData[1] / FACTOR), imageData);
	setRow(0, 3, (audioData[1] / FACTOR), imageData);
	setRow(1, 1, (audioData[1] / FACTOR), imageData);
	setRow(1, 2, (audioData[1] / FACTOR), imageData);
	setRow(2, 0, (audioData[1] / FACTOR), imageData);
	setRow(2, 1, (audioData[1] / FACTOR), imageData);

	setRow(0, 4, (audioData[2] / FACTOR), imageData);
	setRow(0, 5, (audioData[2] / FACTOR), imageData);
	setRow(1, 3, (audioData[2] / FACTOR), imageData);
	setRow(1, 4, (audioData[2] / FACTOR), imageData);
	setRow(2, 2, (audioData[2] / FACTOR), imageData);
	setRow(2, 3, (audioData[2] / FACTOR), imageData);
	setRow(3, 0, (audioData[2] / FACTOR), imageData);
	setRow(3, 1, (audioData[2] / FACTOR), imageData);
	setRow(3, 2, (audioData[2] / FACTOR), imageData);
	setRow(4, 0, (audioData[2] / FACTOR), imageData);
	setRow(4, 1, (audioData[2] / FACTOR), imageData);

	setRow(0, 6, (audioData[3] / FACTOR), imageData);
	setRow(0, 7, (audioData[3] / FACTOR), imageData);
	setRow(1, 5, (audioData[3] / FACTOR), imageData);
	setRow(1, 6, (audioData[3] / FACTOR), imageData);
	setRow(2, 4, (audioData[3] / FACTOR), imageData);
	setRow(2, 5, (audioData[3] / FACTOR), imageData);
	setRow(3, 3, (audioData[3] / FACTOR), imageData);
	setRow(3, 4, (audioData[3] / FACTOR), imageData);
	setRow(4, 2, (audioData[3] / FACTOR), imageData);
	setRow(4, 3, (audioData[3] / FACTOR), imageData);
	setRow(5, 0, (audioData[3] / FACTOR), imageData);
	setRow(5, 1, (audioData[3] / FACTOR), imageData);
	setRow(5, 2, (audioData[3] / FACTOR), imageData);
	setRow(6, 0, (audioData[3] / FACTOR), imageData);
	setRow(6, 1, (audioData[3] / FACTOR), imageData);

	setRow(1, 7, (audioData[4] / FACTOR), imageData);
	setRow(2, 6, (audioData[4] / FACTOR), imageData);
	setRow(2, 7, (audioData[4] / FACTOR), imageData);
	setRow(3, 5, (audioData[4] / FACTOR), imageData);
	setRow(3, 6, (audioData[4] / FACTOR), imageData);
	setRow(4, 4, (audioData[4] / FACTOR), imageData);
	setRow(4, 5, (audioData[4] / FACTOR), imageData);
	setRow(5, 3, (audioData[4] / FACTOR), imageData);
	setRow(5, 4, (audioData[4] / FACTOR), imageData);
	setRow(6, 2, (audioData[4] / FACTOR), imageData);
	setRow(6, 3, (audioData[4] / FACTOR), imageData);
	setRow(7, 0, (audioData[4] / FACTOR), imageData);
	setRow(7, 1, (audioData[4] / FACTOR), imageData);

	setRow(3, 7, (audioData[5] / FACTOR), imageData);
	setRow(4, 6, (audioData[5] / FACTOR), imageData);
	setRow(4, 7, (audioData[5] / FACTOR), imageData);
	setRow(5, 5, (audioData[5] / FACTOR), imageData);
	setRow(5, 6, (audioData[5] / FACTOR), imageData);
	setRow(6, 4, (audioData[5] / FACTOR), imageData);
	setRow(6, 5, (audioData[5] / FACTOR), imageData);
	setRow(7, 2, (audioData[5] / FACTOR), imageData);
	setRow(7, 3, (audioData[5] / FACTOR), imageData);
	setRow(7, 4, (audioData[5] / FACTOR), imageData);

	setRow(5, 7, (audioData[6] / FACTOR), imageData);
	setRow(6, 6, (audioData[6] / FACTOR), imageData);
	setRow(6, 7, (audioData[6] / FACTOR), imageData);
	setRow(7, 5, (audioData[6] / FACTOR), imageData);
	setRow(7, 6, (audioData[6] / FACTOR), imageData);
	setRow(7, 7, (audioData[6] / FACTOR), imageData);
}

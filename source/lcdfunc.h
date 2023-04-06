/****************************************************************************
*  Copyright (c)2023 John Gedde (Amateur radio callsign AD2DK)
*  
*	aslLCD is free software: you can redistribute it and/or modify
*	it under the terms of the GNU Lesser General Public License as
*	published by the Free Software Foundation, either version 3 of the
*	License, or (at your option) any later version.
*
*	aslLCD is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with wiringPi.
*	If not, see <http://www.gnu.org/licenses/>.        
*
*	This file is part of aslLCD:
*	https://github.com/ IT AIN'T THERE YET          
*
*  clockfunc.h
*                                                                          
*  Synopsis:	Header file for lcdfunc.c
*
*  Project:	Allstar Link LCD
*                                                                         
*  File Version History:                                                       
*    Date    |      Eng     |               Description
*  ----------+--------------+------------------------------------------------
*  03/12/23  | John Gedde   |  Original Version
*  
****************************************************************************/ 

#ifndef _LCDFUNC
#define _LCDFUNC

#include <stdint.h>
#include <stdbool.h>

// LCD Color codes
typedef enum
{
	BLC_BL_OFF = 0,
	BLC_RED,
	BLC_GREEN,
	BLC_YELLOW,
	BLC_BLUE,
	BLC_VIOLET,
	BLC_CYAN,
	BLC_WHITE
}BlColors_t;

// Defines for the Adafruit Pi LCD interface board
#define	AF_BASE		100
#define	AF_RED		(AF_BASE + 6)
#define	AF_GREEN	(AF_BASE + 7)
#define	AF_BLUE		(AF_BASE + 8)

#define	AF_E		(AF_BASE + 13)
#define	AF_RW		(AF_BASE + 14)
#define	AF_RS		(AF_BASE + 15)

#define	AF_DB4		(AF_BASE + 12)
#define	AF_DB5		(AF_BASE + 11)
#define	AF_DB6		(AF_BASE + 10)
#define	AF_DB7		(AF_BASE +  9)

#define	AF_SELECT	(AF_BASE +  0)
#define	AF_RIGHT	(AF_BASE +  1)
#define	AF_DOWN		(AF_BASE +  2)
#define	AF_UP		(AF_BASE +  3)
#define	AF_LEFT		(AF_BASE +  4)

#define LCD_I2C_ADDR	0x20

// Defines for button press state
#define BTN_PRESSED			LOW
#define BTN_NOT_PRESSED		HIGH

// Defines for button bits (masks)
typedef enum
{
	BTN_SELECT			= 1,
	BTN_RIGHT			= 2,
	BTN_DOWN			= 4,
	BTN_UP				= 8,
	BTN_LEFT			= 16,
	BTN_ANY				= 0b11111
} BtnStatus_t;

// Button trigger modes
typedef enum
{
	BTN_TRIG_LEVEL	= 0,
	BTN_TRIG_EDGE
} BtnTrigger_t;

typedef enum
{
	LCD_LINE1=0,
	LCD_LINE2
} LcdLine_t;

void lcdWriteLn(const char *str, LcdLine_t line, bool center);
void setBacklightColor(BlColors_t color);
void adafruitLCDSetup(BlColors_t color);
BtnStatus_t waitForButton(uint16_t timeout, BtnStatus_t buttonsEnabled, BtnTrigger_t trigMode);
int16_t initLCD();
uint16_t readButtons();
uint16_t readButtons_edge();
void centerText(const char *intext, char* outtext, uint16_t fieldWidth);
void lcdClearScreen();
void lcdShutdown();
void lcdCursorEnable(bool en);
void lcdPositionCursor(LcdLine_t line, uint8_t pos);
void lcdChar(char c);

#endif
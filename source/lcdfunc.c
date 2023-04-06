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
*  lcdfunc.c
*                                                                          
*  Synopsis:	Thread safe lcd functions to use the adaFruit display kit.
				(e.g. ADA1110) 
*
*  Project:	Allstar Link LCD
*                                                                         
*  File Version History:                                                       
*    Date    |      Eng     |               Description
*  ----------+--------------+------------------------------------------------
*  03/12/23  | John Gedde   |  Original Version
*  
****************************************************************************/

#include "lcdfunc.h"
#include "ini.h"
#include "clockfunc.h"

#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <mcp23017.h>
#include <lcd.h>
#include <iniparser.h>
#include <pthread.h>

// Global lcd handle:
int lcdHandle;

// Mutex for lcd Access
pthread_mutex_t lcdLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutexattr_t mutex_attr;

// Custom character: degree sign
static uint8_t degreeSign[8] = 
{
  0b01100,
  0b10010,
  0b10010,
  0b01100,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
};

/*-----------------------------------------------------------------------------
Function:
	initLCD   
Synopsis:
	Initializes the lcd for our purposes.
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
int16_t initLCD()
{
	FILE *fp;
	char buf[100];	
	
	// Check to see if LCD is there
	sprintf(buf, "i2cdump -y 1 0x%02x b > /tmp/lcdtempfile", LCD_I2C_ADDR);
	system(buf);
	fp=fopen("/tmp/lcdtempfile", "r");
	if (fp)
	{
		while(!feof(fp))
		{
			fgets(buf, sizeof(buf)-1, fp);
			if (strchr(buf, 'X'))
			{
				fprintf(stderr, "aslLCD Error: LCD not present!\n");
				return -1;
			}
		}
	}
	else
	{
		fprintf(stderr, "aslLCD Error: Could not open temp file\n");
		return -1;
	}	
	
	// Initialize wonderful wiringPi 
	// (using wiringpi library by Gordon Henderson)
	// (https://github.com/WiringPi/WiringPi)
	wiringPiSetupSys();
	
	mcp23017Setup(AF_BASE, LCD_I2C_ADDR);
		
	// Setup LCD with initial backlight color from conf file
	adafruitLCDSetup(iniparser_getint(ini, "backlight:color_default", BLC_WHITE));  
	
	//Add custom characters
	lcdCharDef(lcdHandle, 2, degreeSign);
	
	pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK);
	
	if (pthread_mutex_init(&lcdLock, NULL) != 0)
	{
		printf("\n mutex init has failed\n");
		exit(-1);
	}

	return 0;
}

/*-----------------------------------------------------------------------------
Function:
	setBacklightColor   
Synopsis:
	Sets backlight color
Author:
	John Gedde
Inputs:
	BlColors_t color:	bit	2 1 0
							R G B
Outputs:
	None
-----------------------------------------------------------------------------*/
void setBacklightColor(BlColors_t color)
{
	color &= 7;
	
	if (pthread_mutex_lock(&lcdLock)==0)
	{		
		digitalWrite (AF_RED,   !(color & 1));
		digitalWrite (AF_GREEN, !(color & 2));
		digitalWrite (AF_BLUE,  !(color & 4));
		pthread_mutex_unlock(&lcdLock);
	}
}

/*-----------------------------------------------------------------------------
Function:
	initLCD   
Synopsis:
	Initializes the Adafruit LCD board kit
Author:
	John Gedde
Inputs:
	BlColors_t color:	bit	2 1 0
							R G B
Outputs:
	None
-----------------------------------------------------------------------------*/
void adafruitLCDSetup(BlColors_t color)
{
	int i;

	//	Backlight LEDs
	pinMode (AF_RED,   OUTPUT);
	pinMode (AF_GREEN, OUTPUT);
	pinMode (AF_BLUE,  OUTPUT);
  	setBacklightColor(color);

	//	Input buttons
	for (i = 0 ; i <= 4 ; ++i)
	{
		pinMode (AF_BASE + i, INPUT) ;
		pullUpDnControl (AF_BASE + i, PUD_UP) ;	// Enable pull-ups, switches switch to ground
	}

	// Control signals
	pinMode (AF_RW, OUTPUT); 
// TODO -- we need tgo read to know if LCD is there
	digitalWrite (AF_RW, LOW) ;	// Not used with wiringPi - always in write mode

	// The other control pins are initialized with lcdInit()
	lcdHandle = lcdInit(2, 16, 4, AF_RS, AF_E, AF_DB4, AF_DB5, AF_DB6, AF_DB7, 0, 0, 0, 0);

	if (lcdHandle < 0)
	{
		printf("lcdInit failed\n") ;
    	exit(EXIT_FAILURE) ;
	}
}

/*-----------------------------------------------------------------------------
Function:
	readButtons   
Synopsis:
	Reads the current state buttons on the Adafruit lcd board kit.  No
	debouncing occurs here.
Author:
	John Gedde
Inputs:
	None
Outputs:
	uint16_t: bitmask of button(s) pressed.  See BTN_XXXXXX defines.
-----------------------------------------------------------------------------*/
uint16_t readButtons()
{
	int retval=0;

	if (pthread_mutex_lock(&lcdLock)==0)
	{	
		if (digitalRead(AF_SELECT)==BTN_PRESSED)
			retval|=BTN_SELECT;
		if (digitalRead(AF_RIGHT)==BTN_PRESSED)
			retval|=BTN_RIGHT;
		if (digitalRead(AF_LEFT)==BTN_PRESSED)
			retval|=BTN_LEFT;
		if (digitalRead(AF_UP)==BTN_PRESSED)
			retval|=BTN_UP;
		if (digitalRead(AF_DOWN)==BTN_PRESSED)
			retval|=BTN_DOWN;
		pthread_mutex_unlock(&lcdLock);
	}
	
	return retval;
}


/*-----------------------------------------------------------------------------
Function:
	readButtons_edge   
Synopsis:
	Reads the buttons on the Adafruit lcd board kit, but in this version
	we only return a value when the button is first pressed or released.  
	In other words, 'edge triggered.' No debouncing occurs here.
Author:
	John Gedde
Inputs:
	None
Outputs:
	uint16_t: bitmask of button(s) pressed.  See BTN_XXXXXX defines.
-----------------------------------------------------------------------------*/
uint16_t readButtons_edge()
{
	int retval=0;
	static int lastretval=0;
	
	retval=readButtons();
	
	if (lastretval==retval)
		return 0;  // no button presses indicated until something changes
	else
	{
		lastretval=retval;
		return retval;
	}
}

/*-----------------------------------------------------------------------------
Function:
	waitForButton   
Synopsis:
	Wait for a button to be pressed within a selectable time period specified
	by timeout passed in the function call.  If a button press is detected
	the function returns the BtnStatus_t type of the button(s) selected.
	Edge or level triggered	modes can be selected.  Setting timeout to zero 
	means wait forever i.e. no timeout.
Author:
	John Gedde
Inputs:
	uint16_t timeout: timeout value in milliseconds.  0 if no timeout wanted.
	BtnStatus_t buttonsEnabled: an OR-ed combination of the buttons we're 
								interested in using.
	int edgetrigger: 0=level sensitive.  Non-zero=edge sensitive.
Outputs:
	None
-----------------------------------------------------------------------------*/
BtnStatus_t waitForButton(uint16_t timeout, BtnStatus_t buttonsEnabled, BtnTrigger_t trigMode)
{
	BtnStatus_t retval=0;
	uint64_t start, now;
	
	start=now=getClock_ms();

	while (retval==0)
	{
		if (timeout!=0 && now>(start+(uint64_t)timeout))
			break;
		if (trigMode)
			retval=readButtons_edge() & buttonsEnabled;
		else
			retval=readButtons() & buttonsEnabled;
		now=getClock_ms();
		delay(10);
	}
		
	return retval;
}


/*-----------------------------------------------------------------------------
Function:
	lcdWriteLn   
Synopsis:
	Writes a complete line to either line 1 or line 2 of the LCD.  This functions
	truncates or pads as necessary to fit in the 16 characters we have to work
	with.  Text can be either left or center justified.
Author:
	John Gedde
Inputs:
	int hLCD: handle to the LCD display
	const char* str: pointer to the string to write.
	bool center: 0=left justify.  Non-zero=center.
Outputs:
	None
-----------------------------------------------------------------------------*/
void lcdWriteLn(const char *str, LcdLine_t line, bool center)
{
	char temp[17];
	char lcdBuf[17];	
	
	if (pthread_mutex_lock(&lcdLock)==0)
	{	
		if (str && strlen(str)<=16)
		{
			if (center)
			{
				centerText(str, temp, 16);
				sprintf(lcdBuf, "%-16.16s", temp);
			}
			else
				sprintf(lcdBuf, "%-16.16s", str);
			
			if (line==LCD_LINE1)
				lcdPosition(lcdHandle, 0, 0);
			else
				lcdPosition(lcdHandle, 0, 1);
			lcdPuts(lcdHandle, lcdBuf);
		}
		pthread_mutex_unlock(&lcdLock);
	}
}

/*-----------------------------------------------------------------------------
Function:
	centerText   
Synopsis:
	Centers text within a fixed length field.
Author:
	John Gedde
Inputs:
	const char* intext: left justifed input text
	char *outtext: the centered text
	int fieldWidth: the field width for padding.  This function is general
					purpose.  ION the appliatyion the field width is always 16.
	None
-----------------------------------------------------------------------------*/
void centerText(const char *intext, char* outtext, uint16_t fieldWidth) 
{
    int padlen = (fieldWidth - strlen(intext)) / 2;
    sprintf(outtext, "%*s%s%*s", padlen, "", intext, padlen, "");
} 

/*-----------------------------------------------------------------------------    
Function:
	lcdClearScreen   
Synopsis:
	Clears LCD screen
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
void lcdClearScreen()
{
	if (pthread_mutex_lock(&lcdLock)==0)
	{	
		lcdClear(lcdHandle);
		pthread_mutex_unlock(&lcdLock);
	}
}


/*-----------------------------------------------------------------------------    
Function:
	lcdShutdown   
Synopsis:
	Clears LCD and turns off the backlight
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
void lcdShutdown()
{
	lcdClearScreen();
	setBacklightColor(BLC_BL_OFF);
	
	pthread_mutex_destroy(&lcdLock);	
}

/*-----------------------------------------------------------------------------    
Function:
	lcdCursorEnable   
Synopsis:
	Enables/disables the lcd cursor
Author:
	John Gedde
Inputs:
	bool en: enable or disable cursor
Outputs:
	None
-----------------------------------------------------------------------------*/
void lcdCursorEnable(bool en)
{
	//lcdCursor(lcdHandle, en);
	lcdCursorBlink(lcdHandle, en);
}

/*-----------------------------------------------------------------------------    
Function:
	lcdPositionCursor   
Synopsis:
	MOves the lcd cursor to the selected position
Author:
	John Gedde
Inputs:
	LcdLine_t line: Line 1 or line 2
	uint8_t pos: position (left to right)
Outputs:
	None
-----------------------------------------------------------------------------*/
void lcdPositionCursor(LcdLine_t line, uint8_t pos)
{
	lcdPosition(lcdHandle, pos, line);
}

/*-----------------------------------------------------------------------------    
Function:
	lcdChar   
Synopsis:
	Writes a single char at the current position
Author:
	John Gedde
Inputs:
	Lchar c: char to display
Outputs:
	None
-----------------------------------------------------------------------------*/
void lcdChar(char c)
{
	lcdPutchar(lcdHandle, c);
}
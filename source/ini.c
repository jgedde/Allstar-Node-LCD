/****************************************************************************
*  Copyright (c)2023 John Gedde (Amateur radio callsign AD2DK)
*  
*	aslLCD and COSmon are free software: you can redistribute it and/or modify
*	it under the terms of the GNU Lesser General Public License as
*	published by the Free Software Foundation, either version 3 of the
*	License, or (at your option) any later version.
*
*	aslLCD and COSmon are distributed in the hope that it will be useful,
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
*	This file is part of COSmon:
*	https://github.com/ IT AIN'T THERE YET         
*
*  ini.c
*                                                                          
*  Synopsis:	aslLCD specific wrappers around iniparser
*				(using iniparser library from N. Devillard)
*				(https://github.com/ndevilla/iniparser)
*
*  Projects:	Allstar Link LCD, COSmon
*                                                                         
*  File Version History:                                                       
*    Date    |      Eng     |               Description
*  ----------+--------------+------------------------------------------------
*  03/12/23  | John Gedde   |  Original Version
*  
****************************************************************************/

#include "ini.h"

// Global ini file
dictionary *ini=NULL;


/*-----------------------------------------------------------------------------    
Function:
	initIni   
Synopsis:
	Initializes access to the iniparser conf file
Author:
	John Gedde
Inputs:
	char *pName: path and filename to the conf file
Outputs:
	None
-----------------------------------------------------------------------------*/
void initIni(const char *pName)
{
	ini = iniparser_load(pName);
    if (ini==NULL)
	{
        printf("cannot open conf file\n");
        exit(-1);
    }
}

/*-----------------------------------------------------------------------------    
Function:
	iniparser_getstring_16   
Synopsis:
	Read a string from ini file but only let it be 16 char max
Author:
	John Gedde
Inputs:
	const dictionary *d: pointer to the dictionary containing the conf file read.
	const char *key: the section/key
	const char *def: the default value	
Outputs:
	None
-----------------------------------------------------------------------------*/
const char* iniparser_getstring_16(const dictionary *d, const char* key, const char* def)
{
	const char *s;
	static char buf[17]="                ";
	static const char* pBuf=(const char*)buf;
 
	// Get the raw string from the conf file
	s=iniparser_getstring(d, key, def);
	
	// if text is more than 16 characters, it'll be truncated
	sprintf(buf, "%-.16s", s);  

	return pBuf;
}
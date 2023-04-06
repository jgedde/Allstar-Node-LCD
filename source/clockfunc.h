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
*  Synopsis:	Header file for clockfunc.c
*
*  Project:	Allstar Link LCD
*                                                                         
*  File Version History:                                                       
*    Date    |      Eng     |               Description
*  ----------+--------------+------------------------------------------------
*  03/12/23  | John Gedde   |  Original Version
*  
****************************************************************************/ 


// TODO --header
#ifndef _CLOCKFUNCS
#define _CLOCKFUNCS

#include <stdint.h>

uint64_t getClock_ms();


#endif
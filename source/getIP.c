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
*  getIP.c
*                                                                          
*  Synopsis:	Retreives the assigned IP address from either eth0 or wlan0
*
*  Projects:	Allstar Link LCD, COSmon
*                                                                         
*  File Version History:                                                       
*    Date    |      Eng     |               Description
*  ----------+--------------+------------------------------------------------
*  3/24/23		John Gedde		Orginal Version
*  
****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <pthread.h>
#include <wiringPi.h>
#include <ctype.h>
#include <iniparser.h>

#include "getIP.h"
#include "ini.h"


/*-----------------------------------------------------------------------------
Function:
	getIPaddress   
Synopsis:
	Gives the IP address as assigned from either the eth0 or wlan0 interface.
	If both are connected, wifi has priority.
Author:
	John Gedde
Inputs:
	char *buf: pointer to a string that will contain our IP address
Outputs:
	char *buf: 	pointer to a string that will contains our IP address or an
				empty string if we don't have one.
-----------------------------------------------------------------------------*/
void getIPaddress(char *buf)
{
	struct ifaddrs *ifaddr, *ifa;
    int s;
	char host[NI_MAXHOST];
	const char *str;
	char wifiDeviceName[32];
	
	buf[0]='\0';
	
	str=iniparser_getstring(ini, "network devices:wifi interface name", "wlan0");
	strncpy(wifiDeviceName, str, sizeof(wifiDeviceName)-1);
	str=iniparser_getstring(ini, "network devices:wired interface name", "eth0");

    if (getifaddrs(&ifaddr) == -1) 
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_addr == NULL)
            continue;  

        s=getnameinfo(	ifa->ifa_addr,
						sizeof(struct sockaddr_in),
						host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		
		if((strcmp(ifa->ifa_name,str)==0) && (ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
 			sprintf(buf, "%-16s", host);
		}

        if((strcmp(ifa->ifa_name,wifiDeviceName)==0) && (ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
 			sprintf(buf, "%-16s", host);
			break;  // wlan0 has priority over eth0 so that's what we will display!!!
        }
    }		
	freeifaddrs(ifaddr);
}
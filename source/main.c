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
*  main.c
*                                                                          
*  Synopsis:	main function and key supprt stuff for aslLCD   
*
*  Project:	Allstar Link LCD
*                                                                         
*  File Version History:                                                       
*    Date    |      Eng     |               Description
*  ----------+--------------+------------------------------------------------
*  03/12/23  | John Gedde   |  	Original Version
*  04/0623   | John Gedde   |  	1) Increase number of favorites to 20
*								2) Add Disconnect All option to disconnect
*								menu.
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
#include <stdint.h>

#include "ini.h"
#include "main.h"
#include "lcdfunc.h" 
#include "getIP.h"
#include "clockfunc.h"

#define MAX_LOCALNODES_IDX 	9
#define MAX_FAVORITES_IDX 	19
#define MAX_SCRIPT_IDX		9

#define MAX_NODENUM 		99999999
#define MAX_NODNUM_WIDTH 	8

#define MAX_WIFI_COUNT		10
#define MAX_WIFI_NAME_LEN	96
#define MAX_PASSWORD_LEN	64

char strVersion[]="v1.1.0";

typedef enum
{
	STATE_MAIN_MENU=0,
	STATE_WAIT_FOR_RETURN
}States_t;

typedef enum
{
	AST_ABORT_CONNECTION=0,
	AST_MONITOR=2,
	AST_CONNECT=3,
	AST_PERM_MONITOR=12,
	AST_PERM_CONNECT=13	
}AstConnectTypes_t;

static int astCmdLookup[]=
{	AST_CONNECT,
	AST_MONITOR,
	AST_PERM_CONNECT,
	AST_PERM_MONITOR
};

typedef struct
{
	uint32_t nodeNum;
	char connType[10];
}NodeConnection_t;

#define MAX_NODE_INFO_COUNT 20
typedef struct
{
	uint16_t numNodes;
	NodeConnection_t Nodes[MAX_NODE_INFO_COUNT];
}NodeConns_t;	

typedef enum
{
	MM_NODE_CONNECT=0,
	MM_NODE_DISCONNECT,
	MM_SHOW_CONNECTIONS,
	MM_SET_ACTIVE_LOCALNODE,
	MM_SHOW_ACTIVE_LOCALNODE,
	MM_VIEW_NODE_INFO,
	MM_SHUTDOWN_NODE,
	MM_REBOOT_NODE,
	MM_OTHER_INFO,
	MM_WIFI_CONNECT,
	MM_BACKLIGHT_TEST,
	MM_SCRIPTS,
	MM_QUIT_LCD,
	MM_MAX
}MainMenuItems_t;

// Globals
static char strConfProblem[]="CONF PROBLEM!";	
static uint32_t selectedLocalNode=0;
static bool backlightTest=FALSE;
static bool blThreadKill=FALSE;
pthread_t backlightColorStatusThread;

// Local prototypes
static float 				readCPUtemp();
static void 				displayCPUtemp();
static void 				displayIPaddr();
static void 				displayClock();
static void 				displayStartup();
static void 				displayMainMenu(uint8_t menu_num);
static void 				blColorTest();
static void 				*backlightColorStatusThreadFn(void *p);
static void 				nodeConnectSubmenu();
static uint32_t		 		selectFavoriteNode();
static uint32_t		 		enterNodeNum();
static void 				displayActiveLocalNode();
static uint32_t 			selectNodeFromList(uint32_t* list, char names[][17], uint16_t numEntries);
static AstConnectTypes_t 	selectConnectionType();
static void 				displayOtherInfo();
static void 				getNodeConnections(NodeConns_t *pNodeConns);
static void 				showNumConnections();
static void 				showUpTime();
static void 				displayVersion();
static uint16_t				getLocalNodes(uint32_t *list);
static uint32_t		 		initLocalNodeSel();
static void 				shutdownNode();
static void 				scriptsSubmenu();
static void 				nodeDisconnect();
static void 				showWifiConnection();
static void 				selectWifi();
static char 				*strremove(char *str, const char *sub);
static bool 				getWifiPassword(char *pw);
static void 				displayPassword(uint16_t pos, char* pw);
static uint16_t 			findCharIndex(const char *str, char c);
static void 				rebootHandler();
static void 				postConnectReboot();

// TODO -- printfs for error messsages?  Meh.


/*-----------------------------------------------------------------------------
Function:
	backlightColorStatusThreadFn   
Synopsis:
	Thread to control backlight color.   Checks network connection status,
	COS and PTT states and control coor according to settings in asLCD.conf.
	PTT and COS stat is picked up from commands sent to use over a socket port
	(i.e. c, C, p and P)
Author:
	John Gedde
Inputs:
	void *p: arguments
Outputs:
	return val to caller
-----------------------------------------------------------------------------*/
static void *backlightColorStatusThreadFn(void *p)
{
	
#define PTT_UP 			1
#define COS_UP 			2
#define NETWORK_UP 		4
#define FORCE_UPDATE	8
	
	int32_t server_fd, listenSocket;
    struct sockaddr_in address;
    int32_t opt = 1;
    int32_t addrlen = sizeof(address);
    char buffer[6] = { 0 };  
	int32_t portnum;
	char IPaddr[17]={ 0 };
	bool lastBacklightTest=FALSE;
	uint16_t statusBits=0;
	uint16_t lastStatusBits=0;	
	uint16_t divisor, loopCount=0;
	
	BlColors_t colorNetworkUp, colorIdle, colorPTT, colorCOS, colorPTTCOS;

	// Just a delay so user can see the backlight color change from their default to
	// whatever it needs to be based on the status
	delay(1000);

	colorNetworkUp=iniparser_getint(ini, "backlight:color_network_up", BLC_BLUE);
	colorIdle=iniparser_getint(ini, "backlight:color_default", BLC_WHITE);
	colorCOS=iniparser_getint(ini, "backlight:color_COS", BLC_GREEN);
	colorPTT=iniparser_getint(ini, "backlight:color_PTT", BLC_RED);
	colorPTTCOS=iniparser_getint(ini, "backlight:color_PTTCOS", BLC_VIOLET);
	
	divisor=iniparser_getint(ini, "network check:divisor", 10);
	
	// Get port number from conf file
	portnum=iniparser_getint(ini, "backlight:backlight_cmd_port", 0);
	if (portnum==0)
	{
		fprintf(stderr, "aslLCD Error: Backlight control couldn't read portnum from ini file\n");
		return NULL;
	}		
  
    // Create a socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
        printf("aslLCD Error: Backlight command socket creation failed\n");
        return NULL;
    }
    	
	// Bind socket to backlight control port
    if (setsockopt(	server_fd, SOL_SOCKET,
					SO_REUSEADDR | SO_REUSEPORT, &opt,
					sizeof(opt)))
	{
        fprintf(stderr, "aslLCD Error: Error binding to backlight command socket\n");
		return NULL;        
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(portnum);  
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
	{
        fprintf(stderr, "aslLCD Error: Failure binding to backlight command socket\n");
        return NULL;
    }
	
	// Set it to non-blocking
	int flags = fcntl(server_fd, F_GETFL);
	fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

	// set the port up to listen
    if (listen(server_fd, 3) < 0)
	{
        fprintf(stderr, "aslLCD Error: Backlight command port listen failed\n");
        return NULL;
    }
	
	while(!blThreadKill)
	{
		// Check to see if we have an IP address.  That implies we have a network.
		if ((loopCount++ % divisor)==0)
		{
			getIPaddress(IPaddr);
			if (strlen(IPaddr)==0)
				statusBits &= ~NETWORK_UP;
			else
				statusBits |= NETWORK_UP;
		}
		
		// accept a connection
		if ((listenSocket = accept(	server_fd, 
									(struct sockaddr*)&address,
									(socklen_t*)&addrlen)) >= 0) 
		{
			read(listenSocket, buffer, sizeof(buffer));
			
			close(listenSocket);
			listenSocket=-1;
			
			// Process commands.
			switch(buffer[0])
			{
				case 'c':
					statusBits &= ~COS_UP;
					break;
				case 'p':
					statusBits &= ~PTT_UP;
					break;
				case 'C':
					statusBits |= COS_UP;
					break;
				case 'P': 
					statusBits |= PTT_UP;
					break;
				default:
					break;
			}						
		}
		
		// Recover from backlight test
		if (lastBacklightTest==TRUE && backlightTest==FALSE)
			statusBits |= FORCE_UPDATE;
		
		// Only set backlight color if it needs to change to reduce the number of i2c calls	
		// Don't change bl color if bl test is running
		if ((statusBits != lastStatusBits) && !backlightTest)
		{
			// Set the BL color - priority if/else
			if ((statusBits & PTT_UP) && (statusBits & COS_UP))
				setBacklightColor(colorPTTCOS);
			else if (statusBits & PTT_UP)
				setBacklightColor(colorPTT);
			else if (statusBits & COS_UP)
				setBacklightColor(colorCOS);
			else if (statusBits & NETWORK_UP)
				setBacklightColor(colorNetworkUp);
			else
				setBacklightColor(colorIdle);
		}
		lastStatusBits=statusBits;
		lastBacklightTest = backlightTest;
				
		delay(100);	
    }
	
	// close the connected socket
    close(listenSocket);
    // close the listening socket
    shutdown(server_fd, SHUT_RDWR);
	
	return p;
}


/*-----------------------------------------------------------------------------    
Function:
	strremove   
Synopsis:
	Removes a substring from a string
Author:
	stack Overflow member "chqrlie"
Inputs:
	char* str: string to remove substring from
	const char* sub: the substring to remove
Outputs:
	float: temperature in degC
-----------------------------------------------------------------------------*/
static char *strremove(char *str, const char *sub)
{
    char *p, *q, *r;
    if (*sub && (q = r = strstr(str, sub)) != NULL)
	{
        size_t len = strlen(sub);
        while ((r = strstr(p = r + len, sub)) != NULL)
		{
            memmove(q, p, r - p);
            q += r - p;
        }
        memmove(q, p, strlen(p) + 1);
    }
    return str;
}

/*-----------------------------------------------------------------------------    
Function:
	readCPUtemp   
Synopsis:
	Reads the CPU temperature in degC using vcgencmd
Author:
	John Gedde
Inputs:
	None
Outputs:
	float: temperature in degC
-----------------------------------------------------------------------------*/
static float readCPUtemp()
{
	float fVal=-300.0; 
	FILE *fp=NULL;
	
	system("/opt/vc/bin/vcgencmd measure_temp > /tmp/lcdtempfile");
	fp=fopen("/tmp/lcdtempfile", "rw");
	if (fp)
	{
		fscanf(fp, "temp=%f", &fVal);
		fclose(fp);
	}
	else
		fprintf(stderr, "aslLCD Error: Couldn't open lcd temp file\n");
	
	return fVal;
}

/*-----------------------------------------------------------------------------
Function:
	getLocalNodes   
Synopsis:
	Gets a list of connected nodes
Author:
	John Gedde
Inputs:
	uint32_t *list: pointer to a list to store connected node numbers	
Outputs:
	uint16_t return number of connected nodes
-----------------------------------------------------------------------------*/
static uint16_t getLocalNodes(uint32_t *list)
{
	FILE *fp;
	uint16_t idx=0;
	char buf[80];
	uint32_t nodeNum;
	
	if (list)
	{
		system("asterisk -rx \"rpt localnodes\" > /tmp/lcdtempfile");
		
		if ((fp=fopen("/tmp/lcdtempfile", "r"))!=NULL)
		{
			while (!feof(fp))
			{
				fgets(buf, sizeof(buf)-1, fp);
				if (isdigit(buf[0]))
				{
					sscanf(buf, "%u", &nodeNum);
					if (idx<MAX_LOCALNODES_IDX)
						list[idx++]=nodeNum;
				}
			}			
			fclose(fp);
		}
	}
	return idx;
}

/*-----------------------------------------------------------------------------
Function:
	initLocalNodeSel   
Synopsis:
	Pulls the first entry for local node in allstarlcd.conf and sets the
	initialily selected local node.
Author:
	John Gedde
Inputs:
	None
Outputs:
	return uint32_t: first local node in list coming from asterisk
-----------------------------------------------------------------------------*/
static uint32_t initLocalNodeSel()
{
	uint32_t list[MAX_LOCALNODES_IDX+1] = { 0 };
	
	getLocalNodes(list);
	
	return list[0];		
}
	

/*-----------------------------------------------------------------------------
Function:
	displayCPUtemp   
Synopsis:
	Displays the CPU temperature with a 1000 ms update rate in degC and degF
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayCPUtemp()
{
	char buf[17];
	char lcdBuf[17];
	float fVal=0.0;
	const char *s;
	uint16_t buttons;
	
	lcdClearScreen();
	s=iniparser_getstring_16(ini, "headings:hdg_cpu_temp", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
	
	for (;;)
	{
		fVal=readCPUtemp();
		
		if (fVal>-273.15)
		{
			sprintf(buf, "%0.01f\2C, %0.01f\2F", fVal, CtoF(fVal));
			sprintf(lcdBuf, "%-16s", buf);
			lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);			
		}
		
		buttons=waitForButton(1000, (BTN_LEFT | BTN_SELECT), BTN_TRIG_EDGE);

		if (buttons & BTN_LEFT || buttons & BTN_SELECT)
			break;
	}	
}


/*-----------------------------------------------------------------------------
Function:
	displayIPaddr   
Synopsis:
	Displays the IP address.  in case of two IP addresses assigned because user
	connected ethernet and is connected to wifi, wifi takes precidence.
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayIPaddr()
{
	char lcdBuf[17];
	const char *ini_str;
	
	getIPaddress(lcdBuf);
	
	lcdClearScreen();
	ini_str=iniparser_getstring_16(ini, "headings:hdg_ip_addr", strConfProblem);
	lcdWriteLn(ini_str, LCD_LINE1, FALSE);		
	lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);		
	
	for(;;)
	{
		if (waitForButton(0, BTN_SELECT | BTN_LEFT, BTN_TRIG_EDGE))
			break;
	}
}

/*-----------------------------------------------------------------------------
Function:
	displayClock   
Synopsis:
	Displays the system clock.  UP/DOWn buttons toggle between 24 and 12 hour
	clocks.
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
void displayClock()
{
	char buf[17];	
	time_t seconds;
	struct tm *localT;	
	uint8_t clock24;
	const char *s;
	uint16_t buttons;
		
	clock24=iniparser_getint(ini, "options:clock24", 0);

	s=iniparser_getstring_16(ini, "headings:hdg_local_time", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
	
	for (;;)
	{
		seconds=time(NULL);

		localT = localtime(&seconds);
		
		if (clock24)
			sprintf(buf, "%02d:%02d:%02d        ", 
				localT->tm_hour, localT->tm_min, localT->tm_sec);
		else
			sprintf(buf, "%02d:%02d:%02d %s     ", 
				localT->tm_hour % 12, 
				localT->tm_min, 
				localT->tm_sec, 
				(localT->tm_hour>=12 ? "PM" : "AM"));
				
		lcdWriteLn(buf, LCD_LINE2, FALSE);
					
		buttons=waitForButton(50, (BTN_ANY), BTN_TRIG_EDGE);

		if (buttons & BTN_LEFT || buttons & BTN_SELECT)
			break;

		if (buttons & BTN_UP || buttons & BTN_DOWN)
			clock24 = !clock24;
	}		
}
	
	
/*-----------------------------------------------------------------------------
Function:
	displayStartup   
Synopsis:
	Displays the startup screen on the LCD for a time.
	Press any key to jump out early.
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayStartup()
{
	const char *s;
	uint16_t waittime;
	
	lcdClearScreen();
		
	// Startup screen line 1
	s=iniparser_getstring_16(ini, "startup:line1", "Your Call");
	lcdWriteLn(s, LCD_LINE1, TRUE);
		
	// Startup screen line 2
	s=iniparser_getstring_16(ini, "startup:line2", "Allstar Node");
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	waittime=iniparser_getint(ini, "startup:disptime_ms", 3000);
	
	// Allow press any button to skip wait
	waitForButton(waittime, BTN_ANY, BTN_TRIG_EDGE);
}

#ifdef ENABLE_FUTURE_FEATURE
/*-----------------------------------------------------------------------------
Function:
	displayFutureFeature   
Synopsis:
	Displays furture feature note for stuff I have not implemented yet
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayFutureFeature()
{
	const char* s;
	
	s=iniparser_getstring_16(ini, "messages:msg_future_feature", strConfProblem);
	
	lcdClearScreen();
	lcdWriteLn(s, LCD_LINE1, TRUE);
	lcdWriteLn(s, LCD_LINE2, TRUE);
}
#endif

/*-----------------------------------------------------------------------------
Function:
	displayMainMenu   
Synopsis:
	Displays the main menu.
Author:
	John Gedde
Inputs:
	uint8_t menu_num: menu number to display
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayMainMenu(uint8_t menu_num)
{
	const char* s;
	char temp[17];
	
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "main menu:main_menu_top", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	sprintf(temp, "main menu:main_menu%d", menu_num);
	s = iniparser_getstring_16(ini, temp, strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
}

/*-----------------------------------------------------------------------------
Function:
	blColorTest   
Synopsis:
	Backlight color test.
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void blColorTest()
{
	BlColors_t blc;
	const char *s;
	uint16_t buttons;
	char key[32];
	
	backlightTest=TRUE;
			
	s=iniparser_getstring_16(ini, "headings:hdg_bl_test", strConfProblem);
	lcdClearScreen();
	lcdWriteLn(s, LCD_LINE1, FALSE);
	
	blc=iniparser_getint(ini, "backlight:color_default", BLC_WHITE);
	setBacklightColor(blc);	
	sprintf(key, "color_names:color%d", blc&7);
	s=iniparser_getstring_16(ini, key, strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);

	for (;;)
	{
		buttons = waitForButton(1000, (BTN_ANY), BTN_TRIG_EDGE);
		
		if (buttons & BTN_UP)
		{
			setBacklightColor(++blc);
			sprintf(key, "color_names:color%d", blc&7);
			s=iniparser_getstring_16(ini, key, strConfProblem);
			lcdWriteLn(s, LCD_LINE2, TRUE);
		}
		else if (buttons & BTN_DOWN)
		{
			setBacklightColor(--blc);
			sprintf(key, "color_names:color%d", blc&7);
			s=iniparser_getstring_16(ini, key, strConfProblem);
			lcdWriteLn(s, LCD_LINE2, TRUE);
		}
		else if (buttons & BTN_LEFT || buttons & BTN_SELECT)
			break;
	}
		
	blc=iniparser_getint(ini, "backlight:color_default", BLC_WHITE);
	setBacklightColor(blc);
	backlightTest=FALSE;
}

/*-----------------------------------------------------------------------------
Function:
	selActiveLocalNode   
Synopsis:
	Functionality to allow user to select the active local node for reading
	data or for conects and disconnects.
Author:
	John Gedde
Inputs:
	None
Outputs:
	Glocal variable: selectedLocalNode
-----------------------------------------------------------------------------*/
void selActiveLocalNode()
{
	uint32_t list[MAX_LOCALNODES_IDX+1] = { 0 };
	uint16_t numNodes;
	uint16_t buttons;
	uint16_t idx=0;
	char lcdBuf[17];
	const char *s;
	
	lcdClearScreen();
		
	s=iniparser_getstring_16(ini, "headings:hdg_sel_local_node", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "messages:msg_getting_node_list", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	numNodes=getLocalNodes(list);
	
	if (numNodes==0)
	{
		selectedLocalNode=0;
		fprintf(stderr, "aslLCD Warning: No local nodes set-up on device\n");
	}
	else
	{
		sprintf(lcdBuf, "%u", list[0]);
		lcdWriteLn(lcdBuf, LCD_LINE2, TRUE);
		for (;;)
		{
			buttons=waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
			
			if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
			{
				if (buttons & BTN_UP)
				{
					if (idx<numNodes)
						idx=0;
					else idx++;
				}
				else if (idx>0)
					idx--;
				else idx=numNodes-1;
				
				sprintf(lcdBuf, "%u", list[idx]);
				lcdWriteLn(lcdBuf, LCD_LINE2, TRUE);
			}
			if (buttons & BTN_SELECT)
			{
				selectedLocalNode=list[idx];
				break;
			}
			else if (buttons & BTN_LEFT)
				break;
		}
	}	
}

/*-----------------------------------------------------------------------------
Function:
	displayActiveLocalNode   
Synopsis:
	Displays the currently selected local node.
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayActiveLocalNode()
{
	const char *s;
	char lcdBuf[17];	
	
	s=iniparser_getstring_16(ini, "headings:hdg_active_local_node", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);	
	
	sprintf(lcdBuf, "%u", selectedLocalNode);
	lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);
}	

/*-----------------------------------------------------------------------------
Function:
	selectConnectionType   
Synopsis:
	Allows user to selects the desired node conection type using lcd Menus
Author:
	John Gedde
Inputs:
	None	
Outputs:
	AstConnectTypes_t: the selected connection type
-----------------------------------------------------------------------------*/
AstConnectTypes_t selectConnectionType()
{
	const char *s;
	uint16_t choice=0;
	char buf[32];
	uint16_t buttons;
	
	s=iniparser_getstring_16(ini, "connect disconnect:menu_connection_mode", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "connect disconnect:menu_conn_type0", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	for(;;)
	{
		buttons=waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
		
		if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
		{
			if (buttons & BTN_UP)
			{
				if (choice>=3)
					choice=0;
				else
					choice++;
			}
			else if (choice==0)
				choice=3;
			else
				choice--;
		
			sprintf(buf, "connect disconnect:menu_conn_type%u", choice);
			s=iniparser_getstring_16(ini, buf, strConfProblem);
			lcdWriteLn(s, LCD_LINE2, TRUE);
		}
		else if (buttons & BTN_LEFT)
			return AST_ABORT_CONNECTION;
		else if (buttons & BTN_SELECT)
			break;
	}		
	
	return astCmdLookup[choice];
};

/*-----------------------------------------------------------------------------
Function:
	nodeConnectSubmenu   
Synopsis:
	Implements the node connection submenu
Author:
	John Gedde
Inputs:
	None	
Outputs:
	None
-----------------------------------------------------------------------------*/
static void nodeConnectSubmenu()
{
	const char *s;
	bool enterMode=FALSE;
	uint16_t buttons;
	uint32_t nodeNum=0;
	char astCmd[64];
	AstConnectTypes_t connectType;	
	
	s=iniparser_getstring_16(ini, "connect disconnect:menu_connect", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "connect disconnect:menu_favorites", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	for(;;)
	{
		buttons=waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
		
		if (buttons & BTN_LEFT)
			break;
		
		if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
		{
			enterMode=!enterMode;
			if (enterMode)
				s=iniparser_getstring_16(ini, "connect disconnect:menu_enter_node_num", strConfProblem);
			else
				s=iniparser_getstring_16(ini, "connect disconnect:menu_favorites", strConfProblem);
			lcdWriteLn(s, LCD_LINE2, TRUE);
		}
		
		if (buttons & BTN_SELECT)
		{
			if (enterMode)
				nodeNum=enterNodeNum();
			else
				nodeNum=selectFavoriteNode();
			
			if (nodeNum)
			{
				s=iniparser_getstring_16(ini, "messages:msg_ok_select_type_line1", strConfProblem);
				lcdWriteLn(s, LCD_LINE1, TRUE);
				s=iniparser_getstring_16(ini, "messages:msg_ok_select_type_line2", strConfProblem);
				lcdWriteLn(s, LCD_LINE2, TRUE);
				
				delay(1500);
			
				// Select connection type
				connectType=selectConnectionType();
				
				if (connectType!=AST_ABORT_CONNECTION)
				{
					// try to connect to node
					sprintf(astCmd, "asterisk -rx \"rpt cmd %u ilink %d %u\"", selectedLocalNode, connectType, nodeNum);
					system(astCmd);
				}
				break;
					
			}
			else
				break;
		}
	}
}

/*-----------------------------------------------------------------------------
Function:
	selectNodeFromList   
Synopsis:
	Implements the user interfacea for selecing a node from a list
Author:
	John Gedde
Inputs:
	uint32_t* list: pointer to the list of node numbers
	char **: pointer to an array of nodename strings
	uint16_t: the number of node n umbers in the list	
Outputs:
	uint32_t: the selected node number, or 0 if aborted.
-----------------------------------------------------------------------------*/
static uint32_t selectNodeFromList(uint32_t* list, char names[][17], uint16_t numEntries)
{
	uint32_t nodeNum=0;
	unsigned int idx=0;
	uint16_t buttons;
	char buf[17];
	
	if (list && numEntries!=0)
	{	
		for(;;)
		{
			buttons=waitForButton(0, BTN_ANY, TRUE);

			if ((buttons & BTN_DOWN) || (buttons & BTN_UP))
			{
				if (buttons & BTN_UP)
				{
					if (++idx>=numEntries)
						idx=0;					
				}
				else if (idx==0)
					idx=numEntries-1;
				else
					idx--;			
			
				if (strlen(names[idx])>0)
					lcdWriteLn(names[idx], LCD_LINE2, TRUE);
				else
				{
					sprintf(buf, "%u", list[idx]);
					lcdWriteLn(buf, LCD_LINE2, TRUE);
				}
			}				
			else if (buttons & BTN_SELECT)
			{
				nodeNum=list[idx];
				break;
			}
			else if (buttons & BTN_LEFT)
				break;
		}
	}
	
	return nodeNum;
}

/*-----------------------------------------------------------------------------
Function:
	selectFavoriteNode   
Synopsis:
	Reads a list of favorite nodes from aslLCD.conf and allows user to 
	select one for connection.
Author:
	John Gedde
Inputs:
	None
Outputs:
	return uint32_t: selected node number
-----------------------------------------------------------------------------*/
uint32_t selectFavoriteNode()
{
	const char* s;
	uint16_t buttons;
	uint32_t nodeNum;
	char buf[32];
	uint32_t nodeNums[MAX_FAVORITES_IDX+MAX_LOCALNODES_IDX+2]={ 0 };
	char nodeNames[MAX_FAVORITES_IDX+1][17] = { 0 };
	int NumNodes=0;
	uint16_t i;
	uint32_t list[MAX_LOCALNODES_IDX+1];
	
	s=iniparser_getstring_16(ini, "connect disconnect:menu_choose_nodenum", strConfProblem);
	lcdWriteLn(s, 0, TRUE);
	
	// Build list of node numbers in favorites list
	for (i=0; i<=MAX_FAVORITES_IDX; ++i)
	{
		// From favorites
		sprintf(buf, "favorites:favnode%u", i);
		nodeNum=iniparser_getint(ini, buf, 0);
		if (nodeNum!=0 && nodeNum!=selectedLocalNode && nodeNum<MAX_NODENUM)
		{
			nodeNums[NumNodes]=nodeNum;
			NumNodes++;
			sprintf(buf, "favorites:friendlyName%u", i);
			s=iniparser_getstring_16(ini, buf, "");
			strncpy(nodeNames[i], s, 17);
		}
	}	
	
	s=iniparser_getstring_16(ini, "messages:msg_getting_node_list", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	for (i=0; i<getLocalNodes(list); ++i)
	{
		if (list[i]!=0 && list[i]!=selectedLocalNode && list[i]<MAX_NODENUM)
		{
			nodeNums[NumNodes]=list[i];
			NumNodes++;
		}
	}		
	
	if (NumNodes==0)
	{
		s=iniparser_getstring_16(ini, "messages:msg_no_favorites", strConfProblem);
		lcdWriteLn(s, LCD_LINE2, TRUE);
		for (;;)
		{
			buttons = waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
			if (buttons)
				break;
		}
	}
	else
	{	
		if (strlen(nodeNames[0])>0)
			lcdWriteLn(nodeNames[0], LCD_LINE2, TRUE);
		else
		{
			sprintf(buf, "%u", nodeNums[0]);
			lcdWriteLn(buf, LCD_LINE2, TRUE);
		}
			
		nodeNum=selectNodeFromList(nodeNums, nodeNames, NumNodes);
	}
	
	return nodeNum;		
}

/*-----------------------------------------------------------------------------
Function:
	displayOtherInfo   
Synopsis:
	Implements the display other info menu
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayOtherInfo()
{
	uint16_t item=0;
	const char *s;
	uint16_t buttons;
	
	s=iniparser_getstring_16(ini, "other info menu:menu_select_info", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "other info menu:menu_show_clock", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	for(;;)
	{
		buttons = waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
		
		if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
		{
			if (buttons & BTN_UP)
			{
				if (item>=3)
					item=0;
				else
					item++;
			}
			else if (item==0)
				item=3;
			else
				item--;
			
			switch (item)
			{
				case 0:
					s=iniparser_getstring_16(ini, "other info menu:menu_show_clock", strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
					break;
				case 1:
					s=iniparser_getstring_16(ini, "other info menu:menu_show_ip", strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
					break;
				case 2:
					s=iniparser_getstring_16(ini, "other info menu:menu_show_cpu_temp", strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
					break;
				case 3:
					s=iniparser_getstring_16(ini, "other info menu:menu_aslLCD_version", strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
					break;
				default:
					item=0;
					break;
			}
		}
		else if (buttons & BTN_SELECT)
		{
			switch (item)
			{
				case 0:
					displayClock();
					break;
				case 1:
					displayIPaddr();
					break;
				case 2:
					displayCPUtemp();
					break;
				case 3:
					displayVersion();
					break;
				default:
					item=0;
					break;
			}
			break;
		}
		else if (buttons & BTN_LEFT)
			break;
	}	
}

/*-----------------------------------------------------------------------------
Function:
	displayVersion   
Synopsis:
	Displays the current version of aslLCD
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayVersion()
{
	const char *s;
	
	s=iniparser_getstring_16(ini, "headings:hdg_version", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
	lcdWriteLn(strVersion, LCD_LINE2, FALSE);
	
	for(;;)
	{
		if (waitForButton(0, BTN_SELECT | BTN_LEFT, BTN_TRIG_EDGE))
			break;
	}
}
	
/*-----------------------------------------------------------------------------
Function:
	displayVersion   
Synopsis:
	Retrieves a list of connected nodes and presents a list of nodes that the
	user can disconnect from.
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void nodeDisconnect()
{
	NodeConns_t nodeConns = { 0 };
	const char *s;
	uint16_t buttons;
	uint16_t connIdx=0;
	char lcdBuf[17];
	char astCmd[64];
	
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "connect disconnect:menu_disconnect", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
		
	getNodeConnections(&nodeConns);
	
	if (nodeConns.numNodes==0)
	{
		s=iniparser_getstring_16(ini, "messages:msg_no_connections", strConfProblem);
		lcdWriteLn(s, LCD_LINE2, TRUE);
		for (;;)
		{
			buttons=waitForButton(0, BTN_LEFT | BTN_SELECT, BTN_TRIG_EDGE);
			if (buttons)
				break;
		}
	}
	else		
	{
		sprintf(lcdBuf, "%u", nodeConns.Nodes[0].nodeNum);
		lcdWriteLn(lcdBuf, LCD_LINE2, TRUE);
		
		for (;;)
		{
			buttons = waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
			
			if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
			{				
				if (buttons & BTN_UP)
				{
					if (connIdx>=nodeConns.numNodes)
						connIdx=0;
					else
						connIdx++;
				}
				else if (connIdx==0)
					connIdx=nodeConns.numNodes;
				else
					connIdx--;	
				
				// When connIdx equals the number of nodes (i.e. 1 index higher than we have nodes in the list)
				// present user with a 'disconnect all' option.
				if (connIdx==nodeConns.numNodes)
				{
					s=iniparser_getstring_16(ini, "connect disconnect:menu_disconnect_all", strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
				}
				else
				{
					sprintf(lcdBuf, "%u", nodeConns.Nodes[connIdx].nodeNum);
					lcdWriteLn(lcdBuf, LCD_LINE2, TRUE);
				}
			}
			else if (buttons & BTN_SELECT)
			{
				if (connIdx==nodeConns.numNodes)
					// Disconnect all from selected local node
					sprintf(astCmd, "asterisk -rx \"rpt fun %u *76\"", selectedLocalNode);
				else
					// Disconnect selected node.
					sprintf(astCmd, "asterisk -rx \"rpt cmd %u ilink 11 %u\"", 
								selectedLocalNode, 
								nodeConns.Nodes[connIdx].nodeNum);
						
				system(astCmd);
				break;
			}
			else if (buttons & BTN_LEFT)
				break;
		}		
	}
}

/*-----------------------------------------------------------------------------
Function:
	getNodeConnections   
Synopsis:
	Retrieves a list of connected nodes
Author:
	John Gedde
Inputs:
	None
Outputs:
	NodeConns_t *pNodeConns: NodeConns_t: Num Nodes and list of nodes
-----------------------------------------------------------------------------*/
static void getNodeConnections(NodeConns_t *pNodeConns)
{
	FILE *fp;
	char *token=NULL;
	uint16_t nodeIdx=0;
	char buf[128];
	char *s;
	const char* iniStr;
	
	iniStr=iniparser_getstring_16(ini, "messages:msg_getting_connections", strConfProblem);
	lcdWriteLn(iniStr, LCD_LINE2, TRUE);
	
	if (pNodeConns)
	{
		sprintf(buf, "asterisk -rx \"rpt showvars %u\" > /tmp/lcdtempfile", selectedLocalNode);
		system(buf);
		
		if ((fp=fopen("/tmp/lcdtempfile", "r"))!=NULL)
		{
			while(!feof(fp))
			{
				fgets(buf, sizeof(buf)-1, fp);
				if ((s=strstr(buf, "RPT_ALINKS="))!=NULL)
				{
					token = strtok(s, "=");
					if (token)
					{
						token=strtok(NULL, ",");
						if (token)
							sscanf(token, "%u", (unsigned int*)&pNodeConns->numNodes);
					}
				
					while (token && nodeIdx<MAX_NODE_INFO_COUNT)
					{
						token = strtok(NULL, ",");
						if (token)
						{
							sscanf(token, "%u%s", &(pNodeConns->Nodes[nodeIdx].nodeNum), &(pNodeConns->Nodes[nodeIdx].connType[0]));
							nodeIdx++;
						}
					}	
				}
			}
			fclose(fp);
		}
	}
}


/*-----------------------------------------------------------------------------
Function:
	getNodeConnections   
Synopsis:
	Implements show connected nodes fucntion
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void showConnections()
{
	NodeConns_t nodeConns = { 0 };
	const char *s;
	uint16_t buttons;
	uint16_t connIdx=0;
	char lcdBuf[17];
	
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "headings:hdg_connections", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "messages:msg_getting_connections", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	getNodeConnections(&nodeConns);
	
	if (nodeConns.numNodes==0)
	{
		s=iniparser_getstring_16(ini, "messages:msg_no_connections", strConfProblem);
		lcdWriteLn(s, LCD_LINE2, TRUE);
		for (;;)
		{
			buttons=waitForButton(0, BTN_LEFT | BTN_SELECT, BTN_TRIG_EDGE);
			if (buttons)
				break;
		}
	}
	else
	{
		sprintf(lcdBuf, "%u", nodeConns.Nodes[0].nodeNum);
		lcdWriteLn(lcdBuf, LCD_LINE2, TRUE);
		
		for (;;)
		{
			buttons = waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
			
			if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
			{				
				if (buttons & BTN_UP)
				{
					if (connIdx>=nodeConns.numNodes-1)
						connIdx=0;
					else
						connIdx++;
				}
				else if (connIdx==0)
					connIdx=nodeConns.numNodes-1;
				else
					connIdx--;	
				
				sprintf(lcdBuf, "%u", nodeConns.Nodes[connIdx].nodeNum);
				lcdWriteLn(lcdBuf, LCD_LINE2, TRUE);
			}
			else if ((buttons & BTN_SELECT) || (buttons & BTN_LEFT))
				break;
		}		
	}
}	

/*-----------------------------------------------------------------------------
Function:
	showNumConnections   
Synopsis:
	Implements show number of connected nodes fucntion
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void showNumConnections()
{
	NodeConns_t nodeConns = { 0 };
	const char *s;
	char lcdBuf[17];
	
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "headings:hdg_number_of_conns", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
	
	getNodeConnections(&nodeConns);
	
	sprintf(lcdBuf, "%u", nodeConns.numNodes);
	lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);
	
	for (;;)
	{
		if (waitForButton(0, (BTN_SELECT | BTN_LEFT), BTN_TRIG_EDGE))
			break;
	}
}

/*-----------------------------------------------------------------------------
Function:
	showUpTime   
Synopsis:
	Implements show node up time function
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void showUpTime()
{
	const char *s;
	FILE *fp;
	char strUptime[80];
	char *token;
	
	s=iniparser_getstring_16(ini, "headings:hdg_up_time", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
	
	for (;;)
	{
		system("/bin/uptime > /tmp/lcdtempfile");
		
		fp=fopen("/tmp/lcdtempfile", "r");
		
		if (fp)
		{
			fgets(strUptime, sizeof(strUptime)-1, fp);
			token = strstr(strUptime, "up");
			if (token)
			{
				token=strtok(token, ",");
				if (token)
					lcdWriteLn(token, LCD_LINE2, FALSE);
			}			
		}
		else
			break;
	
		if (waitForButton(200, BTN_LEFT | BTN_SELECT, BTN_TRIG_EDGE))
			break;
	}		
}

/*-----------------------------------------------------------------------------
Function:
	nodeInfoSubmenu   
Synopsis:
	Implements node info submenu
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void nodeInfoSubmenu()
{
	const char *s;
	uint16_t buttons;
	uint16_t item=0;
	
	lcdClearScreen();
	s=iniparser_getstring_16(ini, "node info menu:menu_select_info", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "node info menu:menu_num_conns", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	for(;;)
	{
		buttons = waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
		
		if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
		{
			if (buttons & BTN_UP)
			{
				if (item>=1)
					item=0;
				else
					item++;
			}
			else if (item==0)
				item=1;
			else
				item--;
			
			switch (item)
			{
				case 0:
					s=iniparser_getstring_16(ini, "node info menu:menu_num_conns", strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
					break;
				case 1:
					s=iniparser_getstring_16(ini, "node info menu:menu_up_time", strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
					break;
				default:
					item=0;
					break;
			}
		}
		else if (buttons & BTN_SELECT)
		{
			switch (item)
			{
				case 0:
					showNumConnections();
					break;
				case 1:
					showUpTime();
					break;
				default:
					item=0;
					break;
			}
			break;
		}
		else if (buttons & BTN_LEFT)
			break;
	}
}


/*-----------------------------------------------------------------------------
Function:
	shutdownNode   
Synopsis:
	Implements node shutdown and safe power down
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void shutdownNode()
{
	const char* s;
	uint16_t waitTime;
	uint16_t numNodes=0;
	uint32_t list[MAX_LOCALNODES_IDX+1];
	char aslCmd[64];
	
	lcdClearScreen();
	s=iniparser_getstring_16(ini, "messages:msg_shutdown", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
	
	s=iniparser_getstring_16(ini, "messages:msg_please_wait", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, FALSE);
	
	numNodes=getLocalNodes(list);
	
	// disconnect everything for all localnodes
	for (int i=0; i<numNodes; ++i)
	{
		sprintf(aslCmd, "asterisk -rx \"rpt fun %u *76\"", list[i]);
		system(aslCmd);
		delay(1000);
	}
	system("asterisk -rx \"stop gracefully\"");
	
	waitTime=iniparser_getint(ini, "shutdown:shutdown_wait", 1000);	
	delay (waitTime);
}

/*-----------------------------------------------------------------------------
Function:
	enterNodeNum   
Synopsis:
	Implements manunal entry of node number to connect
Author:
	John Gedde
Inputs:
	None
Outputs:
	uint32_t: node number entered or 0 if abort
-----------------------------------------------------------------------------*/
static uint32_t enterNodeNum()
{
	const char* s;
	uint16_t buttons;
	uint8_t position=MAX_NODNUM_WIDTH-1;
	uint32_t nodeNum=0;
	bool cancel=FALSE;
	char lcdBuf[17];
	uint32_t increment=1;
	
	memset(lcdBuf, '0', sizeof(lcdBuf)-1);
		
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "connect disconnect:menu_set_node_num", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
	
	lcdWriteLn("--------", LCD_LINE2, FALSE);
	
	lcdPositionCursor(LCD_LINE2, position);	
	lcdCursorEnable(TRUE);
	
	for(;;)
	{
		buttons=waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
		
		if ((buttons & BTN_LEFT) || (buttons & BTN_RIGHT))
		{
			if (buttons & BTN_LEFT)
			{
				if (position>0)
				{
					increment*=10;
					position--;
				}
				else
					cancel=TRUE;					
			}
			else if (buttons & BTN_RIGHT)
			{
				if (position<MAX_NODNUM_WIDTH-1 && !cancel)
				{
					increment/=10;
					position++;
				}
				cancel=FALSE;
			}			
		
			if (cancel)
			{
				lcdCursorEnable(FALSE);
				s=iniparser_getstring_16(ini, "messages:msg_cancel_prompt", strConfProblem);
				lcdWriteLn(s, LCD_LINE2, FALSE);
			}
			else
			{
				lcdCursorEnable(TRUE);
				sprintf(lcdBuf, "%8u        ", nodeNum);
				for(unsigned int i=0; i<strlen(lcdBuf); ++i)
				{
					if (isdigit(lcdBuf[i]))
						break;  // found the beginning of the number
					else if (lcdBuf[i]==' ')
						lcdBuf[i]='-';
				}		
				lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);
			}
			lcdPositionCursor(LCD_LINE2, position);
		}
		else if (((buttons & BTN_UP) || (buttons & BTN_DOWN)) && !cancel)
		{
			if (buttons & BTN_UP)
			{
				if (lcdBuf[position]<'9')
					nodeNum+=increment;
			}
			else
			{
				if (lcdBuf[position]>'0')
					nodeNum-=increment;
			}
			sprintf(lcdBuf, "%8u        ", nodeNum);
			for(unsigned int i=0; i<strlen(lcdBuf); ++i)
			{
				if (isdigit(lcdBuf[i]))
					break;  // found the beginning of the number
				else if (lcdBuf[i]==' ')
					lcdBuf[i]='-';
			}				
			lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);
			lcdPositionCursor(LCD_LINE2, position);
		}
		else if (buttons & BTN_SELECT)
		{
			if (cancel)
				nodeNum=0;
			break;
		}
	}
	
	lcdCursorEnable(FALSE);
	
	return nodeNum;
}


/*-----------------------------------------------------------------------------
Function:
	scriptsSubmenu   
Synopsis:
	Implements scripts submenu to run scripts defined in aslLCD.conf
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void scriptsSubmenu()
{
	uint16_t buttons;
	const char* s;
	uint16_t scriptIdx=0;
	bool isBad=FALSE;
	char iniBuf[50];
	char strParam[50];
	char cmd[256];
	uint16_t NumScripts=1;
	int needNodeNum;
			
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "headings:hdg_select_script", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring(ini, "scripts:script_path0", "");
	if (strlen(s)==0)
	{
		s=iniparser_getstring_16(ini, "messages:msg_no_script_path", strConfProblem);
		isBad=TRUE;
	}
	
	s=iniparser_getstring_16(ini, "scripts:script_name0", "");
	if (strlen(s)==0)
	{
		s=iniparser_getstring_16(ini, "messages:msg_no_script_name", strConfProblem);		
		isBad=TRUE;
	}

	lcdWriteLn(s, LCD_LINE2, TRUE);	
	
	if (isBad)
	{
		for(;;)
		{
			if (waitForButton(0, BTN_ANY, BTN_TRIG_EDGE))
				break;
		}
	}
	else
	{
		// Find number of good scripts in conf file
		for(int i=1; i<=MAX_SCRIPT_IDX; ++i)
		{
			isBad=FALSE;
			sprintf(iniBuf, "scripts:script_path%d", i);
			s=iniparser_getstring(ini, iniBuf, "");
			if (strlen(s)==0)
				isBad=TRUE;
			sprintf(iniBuf, "scripts:script_name%d", i);
			s=iniparser_getstring(ini, iniBuf, "");
			if (strlen(s)==0)
				isBad=TRUE;
			if (!isBad)
				NumScripts++;
		}
		
		for (;;)
		{
			buttons = waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
			
			if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
			{
				if (buttons & BTN_UP)
				{
					if (++scriptIdx>=NumScripts)
						scriptIdx=0;					
				}
				else if (scriptIdx==0)
					scriptIdx=NumScripts-1;
				else
					scriptIdx--;				
				
				sprintf(iniBuf, "scripts:script_name%u", scriptIdx);
				s=iniparser_getstring_16(ini, iniBuf, strConfProblem);
				lcdWriteLn(s, LCD_LINE2, TRUE);
			}
			else if (buttons & BTN_SELECT)
			{
				sprintf(iniBuf, "scripts:script_param%u", scriptIdx);
				s=iniparser_getstring(ini, iniBuf, "");
				strncpy(strParam, s, sizeof(strParam)-1);
				sprintf(iniBuf, "scripts:script_path%u", scriptIdx);
				s=iniparser_getstring(ini, iniBuf, "");
				sprintf(iniBuf, "scripts:script_need_node_num%u", scriptIdx);
				needNodeNum=iniparser_getint(ini, iniBuf, 0);
				if (needNodeNum)
					sprintf(cmd, "%s %s %u", s, strParam, selectedLocalNode);
				else
					sprintf(cmd, "%s %s", s, strParam);

				system(cmd);
				
			}
			else if (buttons & BTN_LEFT)
				break;
		}
	}
}	

 
/*-----------------------------------------------------------------------------
Function:
	wifiMenuHandler   
Synopsis:
	Implements wifi connect menus
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void wifiMenuHandler()
{
	const char* s;
	bool showConn=TRUE;
	uint16_t buttons;
		
	lcdClearScreen();
	
	s=iniparser_getstring(ini, "wifi menu:menu_choose_action", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	s=iniparser_getstring(ini, "wifi menu:menu_show_current", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	for (;;)
	{
		buttons=waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
		if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
		{
			showConn=!showConn;
			if (showConn)
				s=iniparser_getstring(ini, "wifi menu:menu_show_current", strConfProblem);
			else
				s=iniparser_getstring(ini, "wifi menu:menu_connect_new", strConfProblem);
			lcdWriteLn(s, LCD_LINE2, TRUE);
		}
		else if (buttons & BTN_LEFT)
			break;
		else if (buttons & BTN_SELECT)
		{
			if (showConn)
				showWifiConnection();
			else
				selectWifi();			
			break;
		}	
	}	
}

	
/*-----------------------------------------------------------------------------
Function:
	showWifiConnection   
Synopsis:
	Handler for show wifi connection function
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void showWifiConnection()
{
	char buf[96];
	FILE *fp=NULL;
	char *pWifiName;
	uint16_t scrollPos=0;
	char lcdBuf[17];
	const char *s;
	char cmdBuf[80];
	uint16_t scrollWait;
	
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "messages:msg_getting_info", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "messages:msg_please_wait", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	s=iniparser_getstring(ini, "wifi connect:search_string", "ESSID:");
	
	sprintf(cmdBuf, "iwconfig wlan0 | grep %s > /tmp/lcdtempfile", s);
	system(cmdBuf);
	
	s=iniparser_getstring_16(ini, "headings:hdg_current_wifi", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);
		
	if ((fp=fopen("/tmp/lcdtempfile", "r"))!=NULL) {
		fgets(buf, sizeof(buf)-1, fp);
		fclose(fp);
		s=iniparser_getstring(ini, "wifi connect:search_string", "ESSID:");
		pWifiName=strstr(buf, s);
		if (pWifiName) {
			strremove(pWifiName, s);
			if (pWifiName[0]=='\"')
				pWifiName++;  // Get rid of leading quote
			s=iniparser_getstring(ini, "wifi connect:no_wifi", "off/any");
			if (strstr(pWifiName, s)!=NULL) {
				// we have no wifi.
				s=iniparser_getstring_16(ini, "messages:msg_no_connections", strConfProblem);
				lcdWriteLn(s, LCD_LINE2, FALSE);
				waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
			}
			else {
				// terminate at last "
				for(unsigned int i=0; i<strlen(pWifiName); ++i)	{
					if (pWifiName[i]=='\"' || iscntrl(pWifiName[i]))
					{
						pWifiName[i]='\0';
						break;
					}
				}
									
				if (strlen(pWifiName)<=16)
				{
					// Just display it
					lcdWriteLn(pWifiName, LCD_LINE2, FALSE);
					waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
				}
				else {
					strcat(pWifiName, "   ");  // add a space at the end for scrolling
					scrollWait=iniparser_getint(ini, "wifi connect:scroll_step_interval_ms", 500);
					// Scroll it
					for(;;)	{
						strncpy(lcdBuf, pWifiName+scrollPos, 16);
						lcdBuf[16]='\0';						
						if (strlen(lcdBuf)<16)
							strncat(lcdBuf, pWifiName, 16-strlen(lcdBuf));

						lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);
						
						if (strlen(pWifiName+scrollPos)>0)
							scrollPos++;
						else
							scrollPos=0;
						if (waitForButton(scrollWait, BTN_ANY, BTN_TRIG_EDGE))
							break;
					}
				}
			}
		}
	}
}

/*-----------------------------------------------------------------------------
Function:
	displayPassword   
Synopsis:
	Handler for display of password while entering one.
Author:
	John Gedde
Inputs:
	uint16_t pos: the position in the password to start displaying from...
	char *pw: the password string.
Outputs:
	None
-----------------------------------------------------------------------------*/
static void displayPassword(uint16_t pos, char* pw)
{
	char lcdBuf[17];
	
	memset(lcdBuf, 0, sizeof(lcdBuf));
	if (pos<16)
		strncpy(lcdBuf, pw, 16);
	else
		strncpy(lcdBuf, pw+pos-16, 16);
	lcdBuf[16]='\0';
	lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);
	
	if (pos>15)
		lcdPositionCursor(LCD_LINE2, 15);
	else
		lcdPositionCursor(LCD_LINE2, pos);
}

/*-----------------------------------------------------------------------------
Function:
	findCharIndex   
Synopsis:
	Finds the index into an array where the first instance of a character is
	present.  This function doesn't handle the case where the character is
	not present.
Author:
	John Gedde
Inputs:
	const char *str: The string to search
	char c: the character to look for
Outputs:
	uint16_t: the array index where the character exists
-----------------------------------------------------------------------------*/
static uint16_t findCharIndex(const char *str, char c)
{
	uint16_t retval=0;
	for (uint16_t i=0; i<strlen(str); ++i)
	{
		if (str[i]==c)
		{
			retval=i;
			break;
		}
	}	
	return retval;
}

/*-----------------------------------------------------------------------------
Function:
	selectWifi   
Synopsis:
	Handler for user to select a wifi network to connect to
Author:
	John Gedde
Inputs:
	None
Outputs:
	None
-----------------------------------------------------------------------------*/
static void selectWifi()
{
	char buf[128];
	const char *s;
	char wifiNames[MAX_WIFI_COUNT][MAX_WIFI_NAME_LEN] = { 0 };
	char *pWifiName;
	FILE *fp;
	uint16_t nameIdx=0;
	uint16_t NumFound=0;
	bool scrollIt=FALSE;
	uint16_t scrollWait;
	char lcdBuf[17];
	uint16_t scrollPos=0;
	uint16_t buttons;
	char pw[MAX_PASSWORD_LEN];
	bool gotPW;
	char cmdBuf[256];
	char displayName[128];
	
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "messages:msg_scanning_for_wifi", strConfProblem);			
	lcdWriteLn(s, LCD_LINE1, TRUE);
	
	s=iniparser_getstring_16(ini, "messages:msg_please_wait", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	// Get list of available wifi from system
	s=iniparser_getstring(ini, "wifi connect:search_string", "ESSID:");	
	sprintf(buf, "iwlist wlan0 scanning | grep %s > /tmp/lcdtempfile", s);
	system(buf);
	
	// lcdtempfile should now have list of available wifi names
	fp=fopen("/tmp/lcdtempfile", "r");
	if (fp)
	{
		// Read each line, up to a maximum number of lines
		while (!feof(fp) && nameIdx<MAX_WIFI_COUNT)
		{
			if (fgets(buf, sizeof(buf)-1, fp))
			{
				pWifiName=strstr(buf, s);  // Search for ESSID: 
				if (pWifiName)
				{
					strremove(pWifiName, s);
					pWifiName++;  // Get rid of leading quote					
					// terminate at last "
					for(uint16_t i=0; i<strlen(pWifiName); ++i)
					{
						if (pWifiName[i]=='\"' || iscntrl(pWifiName[i]))
						{
							pWifiName[i]='\0';
							break;
						}
					}
					if (pWifiName[0])
					{
						// Add to list
						strncpy(wifiNames[nameIdx], pWifiName, MAX_WIFI_NAME_LEN-1);
						nameIdx++;
					}
				}
			}
		}
		NumFound=nameIdx;
		fclose(fp);
	}
	
	if (NumFound==0)
	{
		s=iniparser_getstring_16(ini, "messages:msg_no_wifi_available", strConfProblem);
		lcdWriteLn(s, LCD_LINE1, FALSE);
		waitForButton(0, BTN_ANY, BTN_TRIG_EDGE);
	}
	else
	{
		// Display list of available wifi
		scrollWait=iniparser_getint(ini, "wifi connect:scroll_step_interval_ms", 500);
		nameIdx=0;
		pWifiName=wifiNames[0];
		if (strlen(pWifiName)<=16)
			lcdWriteLn(pWifiName, LCD_LINE2, FALSE);
		else
		{
			strcpy(displayName, pWifiName);
			strcat(displayName, "  ");  // padded spaces for scrolling
			scrollIt=TRUE;
		}
		s=iniparser_getstring_16(ini, "wifi menu:menu_select_ssid", strConfProblem);
		lcdWriteLn(s, LCD_LINE1, TRUE);
		
		for(;;)
		{
			if (scrollIt)
			{
				strncpy(lcdBuf, displayName+scrollPos, 16);
				lcdBuf[16]='\0';						
				if (strlen(lcdBuf)<16)
					strncat(lcdBuf, displayName, 16-strlen(lcdBuf));

				lcdWriteLn(lcdBuf, LCD_LINE2, FALSE);
		
				if (strlen(displayName+scrollPos)>0)
					scrollPos++;
				else
					scrollPos=0;
			}
			buttons = waitForButton(scrollWait, BTN_ANY, BTN_TRIG_EDGE);
			if ((buttons & BTN_UP) || (buttons & BTN_DOWN))
			{
				if (buttons & BTN_UP)
				{
					if (++nameIdx>=NumFound)
						nameIdx=0;					
				}
				else if (nameIdx==0)
					nameIdx=NumFound-1;
				else
					nameIdx--;	
				pWifiName=wifiNames[nameIdx];
				scrollPos=0;
				if (strlen(pWifiName)<=16)
				{
					scrollIt=FALSE;
					lcdWriteLn(pWifiName, LCD_LINE2, FALSE);
				}
				else
				{
					strcpy(displayName, pWifiName);
					strcat(displayName, "  ");  // padded spaces for scrolling
					scrollIt=TRUE;
				}
			}
			else if (buttons & BTN_SELECT)
			{
				gotPW=getWifiPassword(pw);
				if (gotPW)
				{					
					s=iniparser_getstring(ini, "wifi connect:wpa_supplicant_file", "/etc/wpa_supplicant/wlan0.conf");
					sprintf(cmdBuf, "wpa_passphrase \"%s\" \"%s\" >> %s", pWifiName, pw, s);
					system(cmdBuf);
					postConnectReboot();
				}
				break;
			}
			else if (buttons & BTN_LEFT)
				break;
		}
	}
}


/*-----------------------------------------------------------------------------
Function:
	getWifiPassword   
Synopsis:
	Handler for user to select a wifi network to connect to
Author:
	John Gedde
Inputs:
	char *pw: pointer to the password string that will contain the entered
	password.
Outputs:
	bool: TRUE if password was entered, FALSE if user cancelled.
-----------------------------------------------------------------------------*/
static bool getWifiPassword(char *pw)
{
	static const char wifiPasswordChars[]={" 0123456789abcdefghijklmnopqrstuvwxyxABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%&\'()*+-./:;<=>?@[]_"};
	int16_t pos=0;
	uint16_t charIdx=0;
	uint16_t buttons;
	const char* s;
	uint64_t pressStartTime;
	bool lastWasUD=FALSE;
	uint16_t FastUpDownWaitms;
	uint16_t FastUpDownRatems;
	bool retval=FALSE;
	
	FastUpDownWaitms=iniparser_getint(ini, "wifi connect:fast_up_down_wait_ms", 2000);
	FastUpDownRatems=iniparser_getint(ini, "wifi connect:fast_up_down_rate_ms", 200);
	
	memset(pw, ' ', MAX_PASSWORD_LEN-1);
	pw[MAX_PASSWORD_LEN-1]='\0';
	
	lcdClearScreen();
	
	s=iniparser_getstring_16(ini, "headings:hdg_enter_password", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, FALSE);	
	
	lcdCursorEnable(true);
	lcdPositionCursor(LCD_LINE2, 0);
	
	for(;;)
	{
		if (lastWasUD && getClock_ms()>(pressStartTime+FastUpDownWaitms))
		{
			buttons=waitForButton(50, BTN_ANY, BTN_TRIG_LEVEL);
			if (buttons==0)
				lastWasUD=FALSE;
			delay(FastUpDownRatems);
		}
		else
			buttons=waitForButton(50, BTN_ANY, BTN_TRIG_EDGE);
		
		if (buttons & BTN_UP || buttons & BTN_DOWN)
		{
			if (lastWasUD==FALSE)
			{
				pressStartTime=getClock_ms();
			}
			if (buttons & BTN_UP)
			{
				if (++charIdx>=sizeof(wifiPasswordChars)-1)
					charIdx=0;
			}
			else if (charIdx==0)
				charIdx=sizeof(wifiPasswordChars)-2;
			else
				charIdx--;
			pw[pos]=wifiPasswordChars[charIdx];
			displayPassword(pos, pw);
			lastWasUD=TRUE;
		}
		else if (buttons & BTN_LEFT)
		{
			if (pos>-1)
				pos--;
			if (pos==-1)
			{
				s=iniparser_getstring_16(ini, "messages:msg_cancel_prompt", strConfProblem);
				lcdWriteLn(s, LCD_LINE2, FALSE);
				lcdCursorEnable(FALSE);
			}
			else
			{
				displayPassword(pos, pw);
				charIdx=findCharIndex(wifiPasswordChars, pw[pos]);
			}
			lastWasUD=FALSE;
		}
		else if (buttons & BTN_RIGHT)
		{
			lcdCursorEnable(TRUE);
			if (pos<MAX_PASSWORD_LEN-1)
				pos++;
			displayPassword(pos, pw);
			charIdx=findCharIndex(wifiPasswordChars, pw[pos]);
			lastWasUD=FALSE;
		}
		else if (buttons & BTN_SELECT)
		{
			if (pos>-1)
			{
				for(uint16_t i=strlen(pw)-1; i; --i)
				{
					if (isspace(pw[i]))
						pw[i]='\0';
					else
						break;
				}
				if (strlen(pw)<8)
				{
					s=iniparser_getstring_16(ini, "messages:msg_at_least_8_chars", strConfProblem);
					lcdWriteLn(s, LCD_LINE1, FALSE);
					continue;
				}
				retval=TRUE;
			}	
			lcdCursorEnable(FALSE);
			break;
		}		
	}
	return retval;
}		

/*-----------------------------------------------------------------------------
Function:
	postConnectReboot   
Synopsis:
	After a new wifi connection was done, the Pi needs to reboot to actually
	connect.  This gives the user the option of rebooting now or later.
Author:
	John Gedde
Inputs:
	None	
Outputs:
	None
-----------------------------------------------------------------------------*/
static void postConnectReboot()
{
	const char *s;
	bool yesNo=TRUE;
	uint16_t buttons;
	
	s=iniparser_getstring_16(ini, "wifi menu:menu_reboot", strConfProblem);
	lcdWriteLn(s, LCD_LINE1, TRUE);
	s=iniparser_getstring_16(ini, "messages:msg_yes", strConfProblem);
	lcdWriteLn(s, LCD_LINE2, TRUE);
	
	for(;;)
	{
		buttons=waitForButton(50, BTN_ANY, BTN_TRIG_EDGE);
		if (buttons & BTN_UP || buttons & BTN_DOWN)
		{
			yesNo=!yesNo;
			if (yesNo)
				s=iniparser_getstring_16(ini, "messages:msg_yes", strConfProblem);
			else
				s=iniparser_getstring_16(ini, "messages:msg_no", strConfProblem);
			lcdWriteLn(s, LCD_LINE2, TRUE);
		}
		else if (buttons & BTN_SELECT)
		{
			if (yesNo)
				rebootHandler();
			else
				break;
		}
	}
}
	



/*-----------------------------------------------------------------------------
Function:
	rebootHandler   
Synopsis:
	Handler for rebooting the Pi.
Author:
	John Gedde
Inputs:
	None	
Outputs:
	None
-----------------------------------------------------------------------------*/
static void rebootHandler()
{
	const char *s;
	
	blThreadKill=TRUE;
	pthread_join(backlightColorStatusThread, NULL);
	
	lcdShutdown();	
	
	// clean up
	remove("/tmp/lcdtempfile");

	s=iniparser_getstring(ini, "reboot:script", "");
	system(s);	
	
	// Close out ini - if we actually get here
	iniparser_freedict(ini);
}


//////////////////////////////////////////////////////////////////////////////////
/*-----------------------------------------------------------------------------
Function:
	main   
Synopsis:
	c main function for aslLCD
Author:
	John Gedde
Inputs:
	None	
Outputs:
	return val to caller
-----------------------------------------------------------------------------*/
int main()
{
	uint16_t buttons;
	uint16_t menu_num=0;
	States_t state=STATE_MAIN_MENU;
	char temp[32];
	const char* s;
	int16_t threadRes=-1;
	bool done=FALSE;
	bool doShutdown=FALSE;	
	
	//signal(SIGINT, shutdownHandler);
	
	// initialize conf file access. 
	// (using iniparser library from N. Devillard)
	// (https://github.com/ndevilla/iniparser)
	initIni("/etc/aslLCD.conf");
	
	// initialize LCD
	if (initLCD() != 0)
	{
		fprintf(stderr, "aslLCD Error: Error initializing LCD\n");
		exit(-1);
	}
	
	// Check to see if asterisk is up
	if (access("/var/run/asterisk.ctl", F_OK) != 0)
	{
		fprintf(stderr, "aslLCD Error: Asterisk needs to be running first!");
		exit(-1);
	}
	
	// initialize selected local node
	selectedLocalNode=initLocalNodeSel();
		
	menu_num=iniparser_getint(ini, "main menu:default_startup_menu", 0);

	// if enabled, start the backlight control thread
	if (iniparser_getint(ini, "backlight:status_backlight", 0))
	{
		threadRes=pthread_create(&backlightColorStatusThread, NULL, backlightColorStatusThreadFn, NULL);
		if (threadRes!=0)
		{
			fprintf(stderr, "aslLCD Error: Could no create backlight control thread\n");
			exit(-1);
		}
	}

	displayStartup();
	displayMainMenu(menu_num);
	
	// Main loop
	while(!done)
	{
		buttons=waitForButton(50, BTN_ANY, TRUE);
		
		switch (state)
		{
			case STATE_MAIN_MENU:
				// Handle menu changes
				if ((buttons & BTN_UP || buttons & BTN_DOWN))
				{
					if (buttons & BTN_UP)
					{
						if (menu_num<MM_MAX-1)
							menu_num++;
						else
							menu_num=0;
					}						
					else if (menu_num==0)
						menu_num=MM_MAX-1;
					else 
						menu_num--;
					
					sprintf(temp, "main menu:main_menu%d", menu_num);
					s = iniparser_getstring_16(ini, temp, strConfProblem);
					lcdWriteLn(s, LCD_LINE2, TRUE);
				} 
				else if (buttons & BTN_SELECT)
				{
					// Do selected action
					switch (menu_num)
					{
						case MM_SHUTDOWN_NODE:
							done=TRUE;
							doShutdown=TRUE;
							shutdownNode();
							break;	
						case MM_REBOOT_NODE:
							rebootHandler();
							break;
						case MM_WIFI_CONNECT:
							wifiMenuHandler();
							break;					
						case MM_SHOW_ACTIVE_LOCALNODE:
							displayActiveLocalNode();
							state = STATE_WAIT_FOR_RETURN;
							break;						
						case MM_SET_ACTIVE_LOCALNODE:
							selActiveLocalNode();
							break;								
						case MM_VIEW_NODE_INFO:
							nodeInfoSubmenu();
							break;							
						case MM_NODE_CONNECT:
							nodeConnectSubmenu();
							break;							
						case MM_NODE_DISCONNECT:
							nodeDisconnect();
							break;						
						case MM_SHOW_CONNECTIONS:
							showConnections();
							break;							
						case MM_BACKLIGHT_TEST:
							blColorTest();
							break;	
						case MM_SCRIPTS:
							scriptsSubmenu();
							break;	
						case MM_QUIT_LCD:
							done=TRUE;
							break;							
						case MM_OTHER_INFO:
							displayOtherInfo();
							break;												
						default:
							menu_num=0;
							break;
					}
					if (state != STATE_WAIT_FOR_RETURN)
						displayMainMenu(menu_num);
				}
				break;
				
			case STATE_WAIT_FOR_RETURN:
				if (buttons & BTN_LEFT || buttons & BTN_SELECT)
				{
					displayMainMenu(menu_num);					
					state = STATE_MAIN_MENU;
				}
				break;
				
			default:
				// Ought not to ever get here, buuuuut...
				state = STATE_MAIN_MENU;
				menu_num=0;
				displayStartup();			
				break;
		}
	}
	
	blThreadKill=TRUE;
	pthread_join(backlightColorStatusThread, NULL);
	
	lcdShutdown();
	
	// clean up
	remove("/tmp/lcdtempfile");
	
	// Close out ini
	iniparser_freedict(ini);
	
	if (doShutdown)
		system("/usr/bin/poweroff");
	
	return(0);
}


CC=gcc
CFLAGS=-I. -Wall -Wextra

aslLCD: main.o lcdfunc.o ini.o clockfunc.o getIP.o
	$(CC) -Wall -Wextra -o aslLCD main.o lcdfunc.o ini.o clockfunc.o getIP.o $(CFLAGS) -lwiringPi -lwiringPiDev -lpthread -lm -lcrypt -lrt -liniparser


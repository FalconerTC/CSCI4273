#Makefile
CC = gcc
CFLAGS = -Wall
INCLUDE = /usr/lib
LIBS = 

DFC_DIR = client/dfc.c

all: dfc

dfc: 
	$(CC) -o dfc $(DFC_DIR) $(CFLAGS) $(LIBS)

clean:
	rm -f dfc

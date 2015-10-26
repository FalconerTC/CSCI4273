#Makefile
CC = gcc
CFLAGS = -lssl -lcrypto -lm
INCLUDE = /usr/lib
LIBS = 

DFC_DIR = client/dfc.c
DFS_DIR = server/dfs.c

all: dfc dfs

dfc: 
	$(CC) -o dfc $(DFC_DIR) $(CFLAGS) $(LIBS)
dfs: 
	$(CC) -o dfs $(DFS_DIR) $(CFLAGS) $(LIBS)

clean:
	rm -f dfc dfs

scrub:
	rm -rf dfc dfs retrieval/* DFS/*

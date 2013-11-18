CC=gcc
CFLAGS=-lpthread -ggdb `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -O2

reconnect.o: reconnect.c
	$(CC) -c -o reconnect.o reconnect.c $(CFLAGS)
reconnect: reconnect.o
	$(CC) -o reconnect reconnect.o $(CFLAGS)

all: reconnect

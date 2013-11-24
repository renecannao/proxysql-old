IDIR =../include
CC=gcc
#CFLAGS=-I$(IDIR) -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -DDEBUG -DPKTALLOC
#CFLAGS=-I$(IDIR) -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -DDEBUG
#CFLAGS=-I$(IDIR) -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -O2
#CFLAGS=-I$(IDIR) -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0`
CFLAGS=-I$(IDIR) -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -DPKTALLOC -O2
#CFLAGS=-I$(IDIR) -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -DPKTALLOC
#CFLAGS=-I$(IDIR) -lpthread -lpcre -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0`
#CFLAGS=-I$(IDIR) -lpthread -lpcre -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -O3 -mtune=native
EXECUTABLE=proxysql

ODIR=obj
LDIR =../lib

LIBS=-lm

_DEPS = proxysql.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = main.o free_pkts.o mem.o debug.o fundadb_hash.o global_variables.o mysql_connpool.o mysql_protocol.o mysql_handler.o network.o queue.o threads.o 
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))


$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

proxysql: $(ODIR) $(OBJ)
	gcc -o $@ $(OBJ) $(CFLAGS) $(LIBS)

$(ODIR):
	mkdir $(ODIR)

all: $(EXECUTABLE)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~ $(EXECUTABLE) 

IDIR =../include
CC=gcc
#CFLAGS=-I$(IDIR) -lpthread -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -O2 -DDEBUG
CFLAGS=-I$(IDIR) -lpthread -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -DDEBUG
#CFLAGS=-I$(IDIR) -lpthread -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -O2
#CFLAGS=-I$(IDIR) -lpthread -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0`
#CFLAGS=-I$(IDIR) -lpthread -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -O2
#CFLAGS=-I$(IDIR) -lpthread -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0`
#CFLAGS=-I$(IDIR) -lpthread -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -O3 -mtune=native -DDEBUG
#CFLAGS=-I$(IDIR) -lpthread -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags gthread-2.0` -O3 -mtune=native
EXECUTABLE=proxysql

ODIR=obj
LDIR =../lib

LIBS=-lm

_DEPS = proxysql.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = main.o free_pkts.o mem.o debug.o sqlite3.o fundadb_hash.o global_variables.o mysql_connpool.o mysql_protocol.o mysql_handler.o network.o queue.o threads.o admin_sqlite.o
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

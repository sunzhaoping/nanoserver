IDIR =./
CC=gcc
CFLAGS=-I$(IDIR)
ODIR=obj
LDIR =/usr/local/lib

LIBS=-lm -lev -lpthread

_DEPS = 
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = server.o options.o xxtea.o md5.o hashmap.o chan.o queue.o pubsub.o

OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

nanoserver: $(OBJ)
	$(CC) -o ./bin/$@ $^ $(CFLAGS) $(LIBS) 

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ bin/*  $(ODIR)/chan/*




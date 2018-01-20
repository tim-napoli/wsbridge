DCLIB = deps
DBUILD = build
DOBJ = $(DBUILD)/obj
DSRC = src

CC = gcc
CFLAGS = -Wall -Werror -std=gnu99 -I$(DCLIB) -I$(DSRC) -L$(DBUILD)
LFLAGS = -ldeps -lpthread

all: make_build_dir $(DBUILD)/libdeps.a $(DBUILD)/wsbridge

make_build_dir:
	mkdir -p $(DBUILD)
	mkdir -p $(DOBJ)
	mkdir -p $(DOBJ)/clib

$(DBUILD)/wsbridge:	$(DOBJ)/wsbridge.o \
					$(DOBJ)/net.o \
					$(DOBJ)/ws.o \
					$(DOBJ)/client.o
	$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS)

$(DOBJ)/%.o: $(DSRC)/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

$(DBUILD)/libdeps.a:
	$(CC) $(CFLAGS) -c $(DCLIB)/b64/encode.c -o $(DOBJ)/clib/b64-encode.o
	$(CC) $(CFLAGS) -c $(DCLIB)/b64/decode.c -o $(DOBJ)/clib/b64-decode.o
	$(CC) $(CFLAGS) -c $(DCLIB)/sha1/sha1.c -o $(DOBJ)/clib/sha1-sha1.o
	ar rcs $@ $(DOBJ)/clib/*.o

clean:
	rm -rf $(DBUILD)

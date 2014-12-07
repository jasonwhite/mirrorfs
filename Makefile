.PHONY: all clean

CFLAGS=-Wall `pkg-config fuse --cflags --libs`

all: mirrorfs

mirrorfs: mirrorfs.c
	gcc ${CFLAGS} $^ -o $@

clean:
	${RM} mirrorfs *.o

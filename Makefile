OPTIMIZATION?=-O0
DEBUG?=-g -ggdb -rdynamic

all: swarm

clean:
		rm -f *.o swarm

.PHONY: all clean

swarm: dlog.o driver.o swarm.o util.o
		$(CC) -o $@ $^ -lpthread -lev -lrt

%.o: %.c
		$(CC) -c -Wall -D_GNU_SOURCE $(OPTIMIZATION) $(DEBUG) $(CFLAGS) $<

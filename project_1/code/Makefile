CC = gcc
CFLAGS = -O2 -Wall -Wextra
LIBS = -lm

all: fire_seq fire_omp

fire_seq: fire_seq.c
	$(CC) $(CFLAGS) -fopenmp fire_seq.c -o fire_seq $(LIBS)

fire_omp: fire_omp.c
	$(CC) $(CFLAGS) -fopenmp fire_omp.c -o fire_omp $(LIBS)

clean:
	rm -f fire_seq fire_omp *.o

.PHONY: all clean

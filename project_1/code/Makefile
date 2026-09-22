CC = gcc
CFLAGS = -O3 -fopenmp -Wall

all: fire_seq fire_omp

fire_seq: fire_seq.c
	$(CC) $(CFLAGS) fire_seq.c -o fire_seq

fire_omp: fire_omp.c
	$(CC) $(CFLAGS) fire_omp.c -o fire_omp

clean:
	del /Q fire_seq.exe fire_omp.exe
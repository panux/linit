all: linitd.o linitctl.o

%.o: %.c Makefile
	cc -g $(basename $@).c -o $@

clean:
	rm *.o

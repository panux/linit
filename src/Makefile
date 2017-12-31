all: linitd.o linitctl.o

%.o: %.c Makefile
	cc $(CFLAGS) $(basename $@).c -o $@

clean:
	rm *.o

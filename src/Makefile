all: linitd.o linitctl.o linit-supd.o linit-sup.o linit-sup-stop.o

%.o: %.c Makefile
	cc $(CFLAGS) $(basename $@).c -o $@

clean:
	rm *.o

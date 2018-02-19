CC=gcc

#ARCH=-mpentiumpro -march=pentiumpro

CFLAGS=-Wall -O2 $(DEFINES) $(ARCH)
OFILES=mtf.o mtfread.o mtfutil.o

.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -o $*.o -c $*.c

mtf: $(OFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OFILES)

mtf.o: mtf.c mtf.h

mtfread.o: mtfread.c

mtfutil.o: mtfutil.c

clean:
	rm -f $(OFILES) mtf core *.dmp log

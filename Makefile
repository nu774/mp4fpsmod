CC=c++

OBJS=main.o mp4filex.o mp4trackx.o mp4v2wrapper.o strcnv.o utf8_codecvt_facet.o
LIBS=mp4v2/.libs/libmp4v2.a
INCLUDES=-I mp4v2 -I mp4v2/include -I mp4v2/src

.SUFFIXES: .cpp

all: mp4fpsmod

mp4fpsmod: $(OBJS) $(LIBS)
	$(CC) -o $@ $(OBJS) $(LIBS)
	strip $@

.cpp.o:
	$(CC) -c -O -Wall $(INCLUDES) $<

clean:
	rm -f *.o

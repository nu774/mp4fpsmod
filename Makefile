OBJS=main.o mp4filex.o mp4trackx.o mp4v2wrapper.o strcnv.o utf8_codecvt_facet.o
LIBS=mp4v2/.libs/libmp4v2.a
INCLUDES=-I mp4v2 -I mp4v2/include -I mp4v2/src

CXXFLAGS = -O2 -Wall $(INCLUDES)

all: mp4fpsmod

mp4fpsmod: $(OBJS) $(LIBS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f *.o

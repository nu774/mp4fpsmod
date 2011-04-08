OBJS=main.o mp4filex.o mp4trackx.o mp4v2wrapper.o strcnv.o utf8_codecvt_facet.o
INCLUDES=-I mp4v2 -I mp4v2/include -I mp4v2/src

CPPFLAGS =-DMP4V2_USE_STATIC_LIB $(INCLUDES)
CXXFLAGS = -O2 -Wall

all: mp4fpsmod

mp4fpsmod: $(OBJS) $(LIBS)
	$(CXX) -o $@ $(OBJS) $(LIBS) -lmp4v2 $(LIBPATH)

clean:
	$(RM) -f *.o

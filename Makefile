bin_PROGRAM = mp4fpsmod
man1_MANS   = man/$(bin_PROGRAM).1
doc_DATA    = README.rst


DESTDIR := /usr/local
bin_DESTDIR  = $(DESTDIR)/bin
man1_DESTDIR = $(DESTDIR)/share/man/man1
doc_DESTDIR  = $(DESTDIR)/share/doc/$(bin_PROGRAM)


OBJS=src/main.o               \
     src/mp4filex.o           \
     src/mp4trackx.o          \
     src/mp4v2wrapper.o       \
     src/strcnv.o             \
     src/utf8_codecvt_facet.o \
     src/version.o


INCLUDES = -I mp4v2 -I mp4v2/include -I mp4v2/src
CPPFLAGS = -DMP4V2_USE_STATIC_LIB $(INCLUDES)
CXXFLAGS = -O2 -Wall
LIBPATH  = -L./mp4v2/.libs


all: libmp4v2 mp4fpsmod
	gzip -fk9 $(man1_MANS)

configure:
	cd mp4v2 && ./make_configure
	cd mp4v2 && ./configure --enable-shared=no --disable-util

libmp4v2:
	cd mp4v2 && $(MAKE)

mp4fpsmod: $(OBJS) $(LIBS)
	$(CXX) -o $@ $(OBJS) $(LIBS) -lmp4v2 $(LIBPATH)

clean:
	rm -f $(bin_PROGRAM) src/*.o *.gz man/*.gz
	cd mp4v2 && $(MAKE) clean

distclean: clean
	cd mp4v2 && $(MAKE) distclean

strip:
	strip $(bin_PROGRAM)

install:
	gzip -fk9 $(doc_DATA)
	install -d $(bin_DESTDIR)
	install -d $(man1_DESTDIR)
	install -d $(doc_DESTDIR)
	install -m775 $(bin_PROGRAM)  $(bin_DESTDIR)
	install -m644 $(man1_MANS).gz $(man1_DESTDIR)
	install -m644 $(doc_DATA).gz  $(doc_DESTDIR)



ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects
SUBDIRS = mp4v2

bin_PROGRAMS = mp4fpsmod

mp4fpsmod_SOURCES = \
    src/main.cpp               \
    src/mp4filex.cpp           \
    src/mp4trackx.cpp          \
    src/mp4v2wrapper.cpp       \
    src/strcnv.cpp             \
    src/utf8_codecvt_facet.cpp \
    src/version.cpp

dist_man_MANS = man/mp4fpsmod.1

AM_CPPFLAGS = \
    -I $(top_srcdir)/mp4v2         \
    -I $(top_builddir)/mp4v2       \
    -I $(top_srcdir)/mp4v2/include \
    -I $(top_srcdir)/mp4v2/src
mp4fpsmod_LDADD = -l mp4v2 -L mp4v2/.libs

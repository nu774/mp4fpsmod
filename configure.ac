m4_define([VERSION_CPP],m4_esyscmd([cat src/version.cpp]))
changequote({{,}})dnl
m4_define({{XX_VERSION}},m4_bregexp(VERSION_CPP,{{^return.*"\(.+\)";}},{{\1}}))
changequote([,])dnl

AC_INIT([mp4fpsmod], [XX_VERSION], [honeycomb77@gmail.com])
AX_SUBDIRS_CONFIGURE([mp4v2], [[--disable-shared], [--enable-static], [--disable-util]])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign])

AC_PROG_CXX

AC_CONFIG_FILES([Makefile])

AC_OUTPUT

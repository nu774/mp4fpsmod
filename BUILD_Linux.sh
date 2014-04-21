#!/bin/sh -e

cd mp4v2
./make_configure
./configure --enable-shared=no --disable-util
make
cd ..
make LIBPATH=-L./mp4v2/.libs
strip mp4fpsmod

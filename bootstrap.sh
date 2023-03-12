#! /bin/sh -e

echo "Running autoreconf..."
autoreconf -ivf "$@"
cd mp4v2; autoreconf -ivf "$@"; cd ..

echo "Now run './configure' and then 'make' to build mp4fpsmod"


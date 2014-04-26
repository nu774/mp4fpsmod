#! /bin/sh -e

mkdir -p "m4"

echo "Running autoreconf..."
autoreconf -ivf "$@"

echo "Now run './configure' and then 'make' to build mp4fpsmod"


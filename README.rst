=========
mp4fpsmod
=========

What is this?
-------------

You can use this tool to modify existing MP4 movie's fps.

Example
-------

Read foo.mp4, change fps to 25, and save to bar.mp4::

    mp4fpsmod -r 0:25 -o bar.mp4 foo.mp4

Read foo.mp4, change fps of first 300 frames to 30000/1001, next 600 frames to 24000/1001, and rest of the movie to 30000/1001 (producing VFR movie)::

    mp4fpsmod -r 300:30000/1001 -r 600:24000/1001 -r 0:30000/1001 -o bar.mp4 foo.mp4

Usage
-----

::

  -r nframes:fps          use this option to specify fps.
  -o file                 output filename.

"nframes" is the number of frames, which "fps" is applied to. 0 as nframes means "rest of the movie".

You can use integer, or fraction value for "fps".

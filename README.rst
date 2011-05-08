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

To edit timecodes with timecode_v2 file::

    mp4fpsmod -t timecode.txt foo.mp4 -o bar.mp4

To read timecodes of MP4 file and save to timecode_v2 file::

    mp4fpsmod -p timecode.txt foo.mp4

Usage
-----

::

  -o file    Specify MP4 output filename.
  -p file    Output current timecodes into timecode-v2 format.
  -t file    Edit timecodes with timecode-v2 file.
  -x         When given with -t, optimize timecode entries in the file.
  -r nframes:fps
             Directly specify fps with option, and edit timecodes.
             You can specify -r option more than two times to produce
             VFR movie.
             "nframes" is number of frames which \"fps\" is aplied to,
             0 as nframes means "rest of the movie"
             "fps" is a rational or integer. That is, something like
             25 or 30000/1001.
  -c         Enable DTS compression.


About timecode optimization
---------------------------

Consider timecode file like this::

  # timecode format v2
  0
  33
  67
  100
  133
  167
  200
    :

This is the example of timecodes for 30000/1001 fps movie.  
In this case, each timeDelta of entries are 33, 34, 33, 33, 34, 33... 

When -x option specified, and timecodes are integer values, mp4fpsmod tries to divide timecodes into groups, whose entries have very near time delta. Then, average each group's time delta into one floating point value.

mp4fpsmod also tries to do further optimization when -x option specified.
If every timeDelta of frames looks like close enough to one of the well known NTSC or PAL rate, mp4fpsmod takes the latter and do the exact math, instead of floating point calcuration.

You can control these behaviors by -x option. Without -x, literal values in the timecodes_v2 file will be used.

About DTS Compression
---------------------

By default, mp4fpsmod produces rather straightforward DTS.
For example, when you specify -r 300:30000/1001 -r400 24000/1001,

- TimeScale is set to 120000, which is LCM of 30000 and 24000
- DTS is like 0, 4004, 8008,... for first 300 frames.
  For next 400 frames, DTS delta is 5005.
- CTS is like DTS, except that it is arranged in the composition
  order, instead of decoding/frame order.
  Then, CTS values are all delayed to keep DTS <= CTS, for each frames.
- Finally, if CTS of first frame is greater than zero (due to the delay),
  edts/elst is used.

In the mp4 container, stts box will look like this::

    <TimeToSampleEntry SampleDelta="4004" SampleCount="300"/>
    <TimeToSampleEntry SampleDelta="5005" SampleCount="400"/>

This is simple and straightforward, and easy to understand.
However, due to B-frames, CTS will be "delayed" in this simple strategy.
This delay value is, by default, saved into edts/elst box.
If your player handles edts box properly, this is fine.
However, there's many hardware players without edts support.
Therefore, you might find video/audio out of sync in your environment.

DTS compression can be used for that purpose.
If you enable DTS compression with "-c" option, mp4fpsmod produces smaller 
DTS for first several frames, and minimizes the CTS delay without the help of
edts/elst box.

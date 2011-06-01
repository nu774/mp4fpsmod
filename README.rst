=========
mp4fpsmod
=========

What is this?
-------------
Tiny mp4 time code editor.
You can use this for changing fps, delaying audio tracks,
executing DTS compression, extracting time codes of mp4.

Example
-------

Read foo.mp4, change fps to 25, and save to bar.mp4::

    mp4fpsmod -r 0:25 -o bar.mp4 foo.mp4

Read foo.mp4, change fps of first 300 frames to 30000/1001, next 600 frames to 24000/1001, and rest of the movie to 30000/1001 (producing VFR movie)::

    mp4fpsmod -r 300:30000/1001 -r 600:24000/1001 -r 0:30000/1001 -o bar.mp4 foo.mp4

Edit timecodes of foo.mp4 with timecode_v2 described in timecode.txt, and save to bar.mp4::

    mp4fpsmod -t timecode.txt foo.mp4 -o bar.mp4

Same as above example, with DTS compression enabled and timecodes optimization::

    mp4fpsmod -t timecode.txt -x -c foo.mp4 -o bar.mp4

Read timecodes of foo.mp4 and save to timecode.txt::

    mp4fpsmod -p timecode.txt foo.mp4

Execute DTS compression, and save to bar.mp4::

    mp4fpsmod -c foo.mp4 -o bar.mp4

Delay audio by 100ms using edts/elst::

    mp4fpsmod -d 100 foo.mp4 -o bar.mp4

Delay audio by -200ms using DTS/CTS shifting::

    mp4fpsmod -d -200 -c foo.mp4 -o bar.mp4

Usage
-----

::
  -o <file>             Specify MP4 output filename.
  -p, --print <file>    Output current timecodes into timecode-v2 format.
  -t, --tcfile <file>   Edit timecodes with timecode-v2 file.
  -x, --optimize        Optimize timecode
  -r, --fps <nframes:fps>
                        Edit timecodes with the spec.
                        You can specify -r more than two times, to produce
                        VFR movie.
                        "nframes" is number of frames, which "fps" is
                        aplied to.
                        0 as nframes means "rest of the movie"
                        "fps" is a rational or integer.
                        For example, 25 or 30000/1001.
  -c, --compress-dts    Enable DTS compression.
  -d, --delay <n>       Delay audio by n millisecond.

In any cases, the original mp4 is kept as it is (not touched).
-o is required except when you specify -p.
On the other hand, when you specify -p, other options are ignored.

-t and -r are exclusive, and cannot be set both at the same time.
-c, -d, -x can be set standalone, or with -t or -r.

When you specify one of -t, -r, -x, -c, -d, timecode is edited/rewritten.
Otherwise without -p, input is just copied with moov->mdat order, without
timecode editing.

You should always set -c when you set -t, -r, -d, -x, if you want your output
widely playable with video/audio in sync, especially with hardware players.
Read about DTS compression for details.

Beware that mp4fpsmod ignores edts/elst of input,
and when timecode is edited, edts/elst of video/audio tracks are deleted,
and re-inserted as needed.
Therefore, if the input has already some audio delays, you have to always
specify it with -d.


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

When -x option is given, and when timecodes are integer values, mp4fpsmod tries to divide timecodes into groups, whose entries have time delta very close to each other.
Then, average each group's time delta into one floating point value.

mp4fpsmod also tries to do further optimization when -x option specified.
If every timeDelta of frames looks like close enough to one of the well known NTSC or PAL rate, mp4fpsmod takes the latter, and do the exact math, instead of floating point calcuration.

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

In the mp4 container, stts box(which holds DTS delta) will look like this::

    <TimeToSampleEntry SampleDelta="4004" SampleCount="300"/>
    <TimeToSampleEntry SampleDelta="5005" SampleCount="400"/>

Timecodes of this movie will be something like this, if B-frame is used::

    ------------ --------
    DTS          CTS
    ------------ --------
    0            0(I)
    4004         12012(P)
    8008         4004(B)
    12012        8008(B)
    16016        24024(P)
    20020        16016(B)
    ------------ --------

However, this doesn't satisfy DTS <= CTS, for some frames.
Therefore, we have to shift(delay) CTS.  Finally, we get::

    ------------ -----
    DTS          CTS
    ------------ -----
    0            4004
    4004         16016
    8008         8008
    12012        12012
    16016        28028
    20020        20020
    ------------ -----

As you can see, CTS of first frame is non-zero value, therefore has delay of
4004, in timescale unit.
This delay value is, by default, saved into edts/elst box.
If your player handles edts/elst properly, this is fine.
However, there's many players in the wild, which lacks edts support.
If you are using them, you might find video/audio out of sync.

DTS compression comes for this reason.
If you enable DTS compression with "-c" option, mp4fpsmod produces smaller 
DTS at beginning, and minimizes the CTS delay without the help of
edts/elst box.
With DTS compression, DTS and CTS will be something like this::

    ----------- -----
    DTS          CTS
    ----------- -----
    0           0
    2002        12012
    4004        4004
    8008        8008
    12012       24024
    16016       16016
    ----------- -----

About audio delay
-----------------

You can specify audio delay with -d option.
Delay is in milliseconds, and both positive and negative values are valid.

When you don't enable DTS compression with -c, delay is just achieved with
edts/elst setting. If positive, video track's edts is set. Otherwise,
each audio track's edts is set.

When you enable DTS compression, DTS/CTS are directly shifted to reflect
the delay.
When delay is positive, smaller DTS/CTS are assigned for the beginning of
movie, so that video plays faster and audio is delayed,
until it reaches the specified delay time.
Negative delay is achieved mostly like the positive case, except that 
bigger DTS/CTS are used, and video plays slower.

# Introduction

**jack-audio** provides functions to play, record, and (duplex) play/record audio data using the
JACK Audio Connection Kit (or JACK) for GNU Octave.

https://jackaudio.org

The code has been tested on the **Jackdmp JACK implementation for multi-processor machine**
version, also known as JACK2, on Linux.

# Usage

Get information from the JACK audio server:

```
> Fs_hz = jinfo;
|------------------------------------------------------
|
| JACK engine sample rate: 48000 [Hz]
|
| Current JACK engine CPU load: 0.170818 [%]
|
|------------------------------------------------------
|         Input ports:
|------------------------------------------------------
|        system:playback_1 [physical]
|        system:playback_2 [physical]
|------------------------------------------------------
|         Output ports:
|------------------------------------------------------
|        system:capture_1 [physical]
|        system:capture_2 [physical]
|------------------------------------------------------
```

Gererate a 5 seconds of (white noise) stereo input signal and play it:

```
> U = single(0.5*rand(5*Fs_hz, 2)-0.25);
> jplay(U,['system:playback_1'; 'system:playback_2']);
```

Record 5 seconds of stereo data:

```
> num_frames = 5*Fs_hz;
> Y = jrecord(num_frames,['system:capture_1'; 'system:capture_2']);
```

Play and record 5 secons of audio data:

```
> U = single(0.5*rand(5*Fs_hz, 2)-0.25);
> Y = jplayrec(U, ['system:capture_1'; 'system:capture_2'], ['system:playback_1'; 'system:playback_2']);
```

# Building

1. Clone the repository
2. Create a build folder and configure the build:

```
$ cd jack-audio
$ mkdir build && cd build
$ cmake -DCMAKE_CXX_FLAGS="-02 -Wall" ..
```

3. Build it

```
$ make -j4
```

4. Add the build folder to you Octave path.

```
$ addpath('<YOUR_JACK-AUDIO_FOLDER>/build/oct');
```

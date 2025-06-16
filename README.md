```
      _                 _                ____  ____
     / \               (_)              |_  _||_  _|
    / _ \     _ .--.   __   _ .--..--.    \ \  / /
   / ___ \   [ `.-. | [  | [ `.-. .-. |    > `' <
 _/ /   \ \_  | | | |  | |  | | | | | |  _/ /'`\ \_
|____| |____|[___||__][___][___||__||__]|____||____|
```

# AnimX

## License
`AnimX` is licensed under the GNU General Public License v2 or later. See [COPYING](COPYING) for details.
`AnimX` uses FFmpeg libraries, which are also licensed under GPL v2+.

`AnimX` links to FFmpeg libraries, licensed under GPL v2+. FFmpeg source code is available via your Linux distribution's package manager (emerge, apt, dnf, etc.) or at https://ffmpeg.org/download.html.

__Note__: `AnimX` is currently WIP. It may not function properly. Use at your own risk!

## About
`AnimX` (Animated wallpapers for X) aims to be a program that can apply images _and_ videos as a background for the Xorg server background.

It can be ran either as a daemon and put into the background, or as the current running process.

## Compiling

The following shows how to compile and install `AnimX`.

### Requirements

* A `C` compiler
* `ffmpeg` - https://ffmpeg.org/download.html (compiled with GNU General Public License v2)
  * `libavcodec/avcodec.h`
  * `libavformat/avformat.h`
  * `libavutil/imgutils.h`
  * `libswscale/swscale.h`
* `X11` - available from your package manager
* `autotools` - available from GNU Coreutils

### Compilation
```
cd AnimX
autoreconf --install
./configure
make
```

### Install
```
sudo make install
```

### Uninstall
```
sudo make uninstall
```

## How to Use

As said in the `About` section, it can be ran normally or as a daemon. To run it normally, simply
invoke `AnimX <path/to/wallpaper.{jpg,jpeg,png,mp4,mkv}>`. If using an image as the wallpaper, it will apply that
image then immediately exit. If it is a video, it will start running the video on your desktop and will
not exit until `SIGTERM` is sent.

Example:
```
AnimX Pictures/pic1.jpg
```
```
AnimX Videos/vid1.mp4
```

There are two methods of loading videos:
1. `--mode=stream` - (default) generates frames and immediately applies those frames to the background (CPU intensive)
2. `--mode=load` - generate all frames up front then start displaying those frames (Memory intensive).

__Note__: It is recommended to use videos < 15 seconds.

To run as a daemon, use the `-d` or `--daemon` flag. After running this, all subsequent calls to `AnimX` will send
messages to the running daemon and will update dynamically. To stop the daemon, issue the `--stop` flag.

__Note__: When supplying the filepath to the daemon, make sure to provide non-relative paths.

Example:
```
AnimX -d
AnimX ~/Videos/vid1.mp4
AnimX --mode=load
AnimX --stop
```
or
```
AnimX -d ~/Videos/vid1.mp4 --mode=load
AnimX --stop
```

__Note__: Every time `AnimX` is used, it writes it's current configuration to
`~/.AnimX`. This file is used for the `--restore` flag, and is not be modified
by hand. If you accidentally delete it, it will not break anything - you will
just lose your last configuration.

See the `--help` flag for all available commands.

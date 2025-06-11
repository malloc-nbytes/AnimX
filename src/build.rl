#!/usr/local/bin/earl

module Build

set_flag("-xe");

@const let cc = "cc";
@const let cname = "-o awx";
@const let cflags = "$(pkg-config --cflags libavcodec libavformat libavutil libswscale imlib2 x11 xrandr) -O3 -Iinclude/";
@const let clibs = "$(pkg-config --libs libavcodec libavformat libavutil libswscale imlib2 x11 xrandr)";
@const let cfiles = "*.c";

$f"{cc} {cflags} {cname} {cfiles} {clibs}";

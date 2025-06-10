#!/usr/local/bin/earl

module Build

set_flag("-xe");

let cc = "cc";
let cname = "-o main";
let cflags = "$(pkg-config --cflags libavcodec libavformat libavutil libswscale imlib2 x11 xrandr) -ggdb -O0";
let clibs = "$(pkg-config --libs libavcodec libavformat libavutil libswscale imlib2 x11 xrandr)";
let cfiles = "*.c";

$f"{cc} {cflags} {cname} {cfiles} {clibs}";

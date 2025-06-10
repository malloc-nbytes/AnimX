#!/usr/local/bin/earl

module Build

set_flag("-x");

let debug = false;
try { debug = ("g", "ggdb", "d", "debug").contains(argv()[1]); }

if debug {
    $"cc -ggdb -O0 -o main main.c -lX11 -lImlib2 -lXrandr -lXext -lavformat -lavcodec -lswscale -lavutil";
} else {
    $"cc -o main main.c -lX11 -lImlib2 -lXrandr -lXext -lavformat -lavcodec -lswscale -lavutil";
}


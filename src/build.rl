#!/usr/local/bin/earl

module Build

import "std/colors.rl"; as clr
import "std/system.rl"; as sys
import "std/datatypes/list.rl";
import "std/script.rl"; as scr

set_flag("-xe");

let debug, clean, install, uninstall = (false, false, false, false);
try { debug = ("g", "ggdb", "debug", "d").contains(argv()[1]); }
try { clean = argv()[1] == "clean"; }
try { install = argv()[1] == "install"; }
try { uninstall = argv()[1] == "uninstall"; }

if !uninstall && !install && !clean {
    println(clr::Te.Bold, "======= ", clr::Tfc.Green, "Building AnimX", clr::Te.Reset, clr::Te.Bold, " =======", clr::Te.Reset);

    let deps_ok = true;
    let needed = [];
    println(Colors::Tfc.Green, "*** Checking Dependencies:", Colors::Te.Reset);
    with deps = ("pkg-config", "ffmpeg", "xrandr", "Xorg")
    in foreach d in deps {
        print(d, "...");
        if !scr::program_exists(d) {
            needed += [d];
            println(clr::Tfc.Red, " no", clr::Te.Reset);
        } else {
            println(clr::Tfc.Green, " ok", clr::Te.Reset);
        }
    }

    if len(needed) > 0 {
        deps_ok = false;
        println(clr::Tfc.Red, f"Dependencies {needed} is required for building AnimX", clr::Te.Reset);
    }

    needed = [];
    println(Colors::Tfc.Green, "*** Checking Headers:", Colors::Te.Reset);
    with deps = ("libavcodec", "libavformat", "libavutil", "libswscale", "imlib2", "x11", "xrandr")
    in foreach d in deps {
        print(d, "...");
        $f"pkg-config {d} --exists && echo 1 || echo 0" |> let e;
        if e != "1" {
            needed += [d];
            println(clr::Tfc.Red, " no", clr::Te.Reset);
        } else {
            println(clr::Tfc.Green, " ok", clr::Te.Reset);
        }
    }

    if len(needed) > 0 {
        deps_ok = false;
        println(clr::Tfc.Red, f"Headers {needed} are required for building AnimX", clr::Te.Reset);
    }

    if !deps_ok { exit(1); }
}

$"pkg-config --cflags libavcodec libavformat libavutil libswscale imlib2 x11 xrandr" |> let cflags;
$"pkg-config --libs libavcodec libavformat libavutil libswscale imlib2 x11 xrandr" |> let libs;
@const let ccomb_flags = f"{cflags} -O3 -Iinclude/";

@const let cc = "cc";
@const let cname = "-o AnimX";
@const let cfiles = sys::ls(".")
    .filter(|f| {
        with parts = sys::name_and_ext(f)
        in return
            !sys::isdir(f)
            && !("./AnimX", "./build.rl").contains(f)
            && parts[1]
            && parts[1].unwrap() != "o";
    });


if !uninstall && !install && !clean {
    println(Colors::Tfc.Green, "*** Info:", Colors::Te.Reset);
    println(f"cc={cc}");
    println(f"cname={cname}");
    println(f"cfiles={cfiles}");
    println(f"cflags={cflags}");
    println(f"libs={libs}");
    println(f"ccomb_flags={ccomb_flags}");
    println(f"debug={debug}");
    println(f"install={install}");
    println(f"uninstall={uninstall}");
}

if install {
    $"sudo cp ./AnimX /usr/local/bin/AnimX";
} else if uninstall {
    $"sudo rm /usr/local/bin/AnimX";
} else if clean {
    let objs = sys::ls(".").filter(|f| {
        with parts = sys::name_and_ext(f)
        in return parts[1] && parts[1].unwrap() == "o";
    });
    foreach o in objs { $f"rm {o}"; }
} else {
    if debug {
        build(cc, ccomb_flags + " -ggdb -O0", cname, libs, cfiles);
    } else {
        build(cc, ccomb_flags, cname, libs, cfiles);
    }
    println(clr::Te.Bold, "======= ", clr::Tfc.Green, "Done", clr::Te.Reset, clr::Te.Bold, " =======", clr::Te.Reset);
}

fn build(cc, flags, name, libs, cfiles) {
    println(Colors::Tfc.Green, "*** Compiling:", Colors::Te.Reset);
    foreach f in cfiles {
        $f"{cc} {flags} -c {f}";
    }
    $f"{cc} {name} *.o {libs}";
}

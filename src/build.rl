#!/usr/local/bin/earl

module Build

import "std/colors.rl"; as clr
import "std/system.rl"; as sys
import "std/datatypes/list.rl";
import "std/script.rl"; as scr

set_flag("-xe");

println(clr::Te.Bold, "======= ", clr::Tfc.Green, "Building awx", clr::Te.Reset, clr::Te.Bold, " =======", clr::Te.Reset);

let debug, install, uninstall = (false, false, false);
try { debug = ("g", "ggdb", "debug", "d").contains(argv()[1]); }
try { install = argv()[1] == "install"; }
try { uninstall = argv()[1] == "uninstall"; }

if !uninstall && !install {
        let deps_ok = true;
        let needed = [];
        println(clr::Tfc.Yellow, "Checking dependencies...", clr::Te.Reset);
        with deps = ("pkg-config", "ffmpeg", "xrandr", "Xorg")
        in foreach d in deps {
                print(clr::Tfc.Yellow, "  ", d, clr::Te.Reset);
                if !scr::program_exists(d) {
                        needed += [d];
                } else {
                        println(clr::Tfc.Green, " [ok]", clr::Te.Reset);
                }
        }

        if len(needed) > 0 {
                deps_ok = false;
                println(clr::Tfc.Red, "Dependencies {needed} is required for building awx", clr::Te.Reset);
        }

        needed = [];
        println(clr::Tfc.Yellow, "Checking headers...", clr::Te.Reset);
        with deps = ("libavcodec", "libavformat", "libavutil", "libswscale", "imlib2", "x11", "xrandr")
        in foreach d in deps {
                print(clr::Tfc.Yellow, "  ", d, clr::Te.Reset);
                $f"pkg-config {d} --exists && echo 1 || echo 0" |> let e;
                if e != "1" {
                        needed += [d];
                } else {
                        println(clr::Tfc.Green, " [ok]", clr::Te.Reset);
                }
        }

        if len(needed) > 0 {
                deps_ok = false;
                println(clr::Tfc.Red, f"Headers {needed} are required for building awx", clr::Te.Reset);
        }

        if !deps_ok { exit(1); }
}

$"pkg-config --cflags libavcodec libavformat libavutil libswscale imlib2 x11 xrandr" |> let cflags;
$"pkg-config --libs libavcodec libavformat libavutil libswscale imlib2 x11 xrandr" |> let ld;
@const let ccomb_flags = f"{cflags} -O3 -Iinclude/ {ld}";

@const let cc = "cc";
@const let cname = "-o awx";
@const let cfiles = List::to_str(sys::ls(".")
        .filter(|f| {
                return !sys::isdir(f) && !("./awx", "./build.rl").contains(f);
        }));

if !uninstall && !install {
        println(f"cc={cc}");
        println(f"cname={cname}");
        println(f"cfiles={cfiles}");
        println(f"cflags={cflags}");
        println(f"ld={ld}");
        println(f"ccomb_flags={ccomb_flags}");
        println(f"debug={debug}");
        println(f"install={install}");
        println(f"uninstall={uninstall}");
}

if install {
        $"sudo cp ./awx /usr/local/bin/awx";
} else if uninstall {
        $"sudo rm /usr/local/bin/awx";
} else {
        if debug {
                $f"{cc} {ccomb_flags} -ggdb -O0 {cname} {cfiles}";
        } else {
                $f"{cc} {ccomb_flags} {cname} {cfiles}";
        }
}

println(clr::Te.Bold, "======= ", clr::Tfc.Green, "Done", clr::Te.Reset, clr::Te.Bold, " =======", clr::Te.Reset);

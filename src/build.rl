#!/usr/local/bin/earl

module Build

import "std/colors.rl"; as clr
import "std/system.rl"; as sys
import "std/datatypes/list.rl";

set_flag("-xe");

println(clr::Te.Bold, "======= Building awx =======", clr::Te.Reset);

let install, uninstall = (false, false);
try { install = argv()[1] == "install"; }
try { uninstall = argv()[1] == "uninstall"; }

$"pkg-config --cflags libavcodec libavformat libavutil libswscale imlib2 x11 xrandr" |> let cflags;
$"pkg-config --libs libavcodec libavformat libavutil libswscale imlib2 x11 xrandr" |> let ld;
@const let ccomb_flags = f"{cflags} -O3 -Iinclude/ {ld}";

@const let cc = "cc";
@const let cname = "-o awx";
@const let cfiles = List::to_str(sys::ls(".")
        .filter(|f| { !sys::isdir(f)
                      && !("./awx", "./build.rl").contains(f); }));

println(clr::Tfc.Green, f"cc={cc}", clr::Te.Reset);
println(clr::Tfc.Green, f"cname={cname}", clr::Te.Reset);
println(clr::Tfc.Green, f"cfiles={cfiles}", clr::Te.Reset);
println(clr::Tfc.Green, f"cflags={cflags}", clr::Te.Reset);
println(clr::Tfc.Green, f"ld={ld}", clr::Te.Reset);
println(clr::Tfc.Green, f"ccomb_flags={ccomb_flags}", clr::Te.Reset);

println(case install of { true = clr::Tfc.Green; _ = clr::Tfc.Red; }, f"install={install}", clr::Te.Reset);
println(case uninstall of { true = clr::Tfc.Green; _ = clr::Tfc.Red; }, f"uninstall={uninstall}", clr::Te.Reset);

if install {
        $"sudo cp ./awx /usr/local/bin/awx";
} else if uninstall {
        $"sudo rm /usr/local/bin/awx";
} else {
        $f"{cc} {ccomb_flags} {cname} {cfiles}";
}

println(clr::Te.Bold, "======= Done =======", clr::Te.Reset);

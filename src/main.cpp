#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <Imlib2.h>

#define ConnectionDisconnected 1

// Set wallpaper for Xorg on a specific monitor
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <monitor_index> <filepath>\n", argv[0]);
        exit(1);
    }

    int monitor_index = atoi(argv[1]);
    char *image_path = argv[2];

    // Initialize X11 display
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Could not open X display\n");
        exit(1);
    }

    int screen = DefaultScreen(display);
    Window root = DefaultRootWindow(display);

    // Initialize Xrandr and get monitor info
    int xrandr_event_base, xrandr_error_base;
    if (!XRRQueryExtension(display, &xrandr_event_base, &xrandr_error_base)) {
        fprintf(stderr, "Xrandr extension not available\n");
        XCloseDisplay(display);
        exit(1);
    }

    XRRScreenResources *screen_res = XRRGetScreenResources(display, root);
    if (!screen_res) {
        fprintf(stderr, "Failed to get screen resources\n");
        XCloseDisplay(display);
        exit(1);
    }

    // Check if monitor_index is valid
    if (monitor_index < 0 || monitor_index >= screen_res->noutput) {
        fprintf(stderr, "Invalid monitor index: %d (available: 0 to %d)\n",
                monitor_index, screen_res->noutput - 1);
        XRRFreeScreenResources(screen_res);
        XCloseDisplay(display);
        exit(1);
    }

    // Get the monitor's geometry
    XRROutputInfo *output_info = XRRGetOutputInfo(display, screen_res, screen_res->outputs[monitor_index]);
    if (!output_info || output_info->connection == ConnectionDisconnected) {
        fprintf(stderr, "Monitor %d is not connected\n", monitor_index);
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_res);
        XCloseDisplay(display);
        exit(1);
    }

    // Get the CRTC info for the monitor's geometry
    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen_res, output_info->crtc);
    if (!crtc_info) {
        fprintf(stderr, "Failed to get CRTC info for monitor %d\n", monitor_index);
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_res);
        XCloseDisplay(display);
        exit(1);
    }

    int mon_x = crtc_info->x;
    int mon_y = crtc_info->y;
    int mon_width = crtc_info->width;
    int mon_height = crtc_info->height;

    // Initialize Imlib2
    imlib_set_cache_size(2048 * 1024);
    imlib_context_set_display(display);
    imlib_context_set_visual(DefaultVisual(display, screen));
    imlib_context_set_colormap(DefaultColormap(display, screen));

    // Load image
    Imlib_Image image = imlib_load_image(image_path);
    if (!image) {
        fprintf(stderr, "Failed to load image %s\n", image_path);
        XRRFreeCrtcInfo(crtc_info);
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_res);
        XCloseDisplay(display);
        exit(1);
    }

    imlib_context_set_image(image);

    // Scale image to monitor size
    imlib_context_set_anti_alias(1);
    Imlib_Image scaled_image = imlib_create_cropped_scaled_image(
        0, 0, imlib_image_get_width(), imlib_image_get_height(),
        mon_width, mon_height);

    if (!scaled_image) {
        fprintf(stderr, "Failed to scale image\n");
        imlib_context_set_image(image);
        imlib_free_image();
        XRRFreeCrtcInfo(crtc_info);
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_res);
        XCloseDisplay(display);
        exit(1);
    }

    imlib_context_set_image(scaled_image);

    // Create a pixmap for the entire root window
    int screen_width = DisplayWidth(display, screen);
    int screen_height = DisplayHeight(display, screen);
    Pixmap pixmap = XCreatePixmap(display, root, screen_width, screen_height,
                                  DefaultDepth(display, screen));
    imlib_context_set_drawable(pixmap);

    // Clear pixmap (to avoid garbage on other monitors)
    XSetForeground(display, DefaultGC(display, screen), 0);
    XFillRectangle(display, pixmap, DefaultGC(display, screen), 0, 0, screen_width, screen_height);

    // Render image to the monitor's region in the pixmap
    imlib_render_image_on_drawable(mon_x, mon_y);

    // Set pixmap as root window background
    XSetWindowBackgroundPixmap(display, root, pixmap);
    XClearWindow(display, root);

    // Update root window properties (for compatibility with some DEs)
    Atom prop_root = XInternAtom(display, "_XROOTPMAP_ID", False);
    Atom prop_esetroot = XInternAtom(display, "ESETROOT_PMAP_ID", False);
    XChangeProperty(display, root, prop_root, XA_PIXMAP, 32, PropModeReplace,
                    (unsigned char *)&pixmap, 1);
    XChangeProperty(display, root, prop_esetroot, XA_PIXMAP, 32, PropModeReplace,
                    (unsigned char *)&pixmap, 1);

    // Flush changes
    XFlush(display);

    imlib_context_set_image(scaled_image);
    imlib_free_image();
    imlib_context_set_image(image);
    imlib_free_image();

    XFreePixmap(display, pixmap);
    XRRFreeCrtcInfo(crtc_info);
    XRRFreeOutputInfo(output_info);
    XRRFreeScreenResources(screen_res);
    XCloseDisplay(display);

    return 0;
}

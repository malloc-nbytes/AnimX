#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <Imlib2.h>

// animate wallpaper for X

// Set wallpaper for Xorg
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filepath>\n", argv[0]);
        exit(1);
    }
    ++argv;

    // Initialize X11 display
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Could not open X display\n");
        exit(1);
    }

    int screen = DefaultScreen(display);
    Window root = DefaultRootWindow(display);

    // Initialize Imlib2
    imlib_set_cache_size(2048 * 1024);
    imlib_context_set_display(display);
    imlib_context_set_visual(DefaultVisual(display, screen));
    imlib_context_set_colormap(DefaultColormap(display, screen));
    imlib_context_set_drawable(root);

    // Load image
    Imlib_Image image = imlib_load_image(*argv);
    if (!image) {
        fprintf(stderr, "Failed to load image %s\n", *argv);
        XCloseDisplay(display);
        exit(1);
    }

    // Set image as context
    imlib_context_set_image(image);

    // Get screen dimensions
    int screen_width = DisplayWidth(display, screen);
    int screen_height = DisplayHeight(display, screen);

    // Scale image to screen size
    imlib_context_set_anti_alias(1);
    Imlib_Image scaled_image = imlib_create_cropped_scaled_image(
        0, 0, imlib_image_get_width(), imlib_image_get_height(),
        screen_width, screen_height);

    if (!scaled_image) {
        fprintf(stderr, "Failed to scale image\n");
        imlib_context_set_image(image);
        imlib_free_image();
        XCloseDisplay(display);
        exit(1);
    }

    imlib_context_set_image(scaled_image);

    // Create a pixmap for the background
    Pixmap pixmap = XCreatePixmap(display, root, screen_width, screen_height,
                                  DefaultDepth(display, screen));
    imlib_context_set_drawable(pixmap);

    // Render image to pixmap
    imlib_render_image_on_drawable(0, 0);

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

    // Clean up
    imlib_context_set_image(scaled_image);
    imlib_free_image();
    imlib_context_set_image(image);
    imlib_free_image();
    XFreePixmap(display, pixmap);
    XCloseDisplay(display);

    return 0;
}

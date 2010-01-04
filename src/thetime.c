/**
 * thetime -- a tiny X app to show the current time
 * Copyright (C) 2009  Oliver Mader <dotb52@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>

/* general xlib vars */
static Display *display = NULL;
static int fd;
static int screen;
static Window root_window;
static Window window = None;
static Visual *visual;
static Colormap colormap;
static XftFont *font;
static XftDraw *draw;
static XftColor color;

/* runtime settings */
static volatile sig_atomic_t running = 1;
static int x = 20, y = 20, width = 10, height = 10;
static int last_update = 0, need_update = 1, update_interval = 1;
static char time_string[56];
static char *time_format = "%T";

/* prototypes */
static void sig_handler(int signal);
static void cleanup();
static void parse_cmdline(int argc, char *argv[]);
static void update_time();
static void draw_time();
static void set_background();

/* lets start here */
int main(int argc, char *argv[])
{
    /* try to set the current locale */
    setlocale(LC_ALL, "");

    /* install a new signal handler */
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);

    /* install a cleanup function */
    atexit(cleanup);

    /* open the default display */
    if ((display = XOpenDisplay(NULL)) == NULL) {
        fprintf(stderr, "Failed to open default display.\n");
        return 1;
    }

    /* get some needed display defaults */
    fd = ConnectionNumber(display);
    screen = DefaultScreen(display);
    visual = DefaultVisual(display, screen);
    colormap = DefaultColormap(display, screen);
    root_window = DefaultRootWindow(display);

    /* try to read the runtime settings from cmdline */
    parse_cmdline(argc, argv);

    /* create window and draw context */
    window = XCreateWindow(display, root_window, 0, 0, width, height, 0,
        CopyFromParent, InputOutput, CopyFromParent, 0, NULL);
    draw = XftDrawCreate(display, window, visual, colormap);

    /* we need to force window position and size */
    XSetWindowAttributes attr = {.override_redirect = True};
    XChangeWindowAttributes(display, window, CWOverrideRedirect, &attr);

    /* set window hints */
    XWindowChanges values = {.stack_mode = Below};
    XConfigureWindow(display, window, CWStackMode, &values);
    long val = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(display, window, XInternAtom(display, "_NET_WM_WINDOW_TYPE",
        False), XA_ATOM, 32, PropModeReplace, (unsigned char *) &val, 1);
    val = 0xffffffff;
    XChangeProperty(display, window, XInternAtom(display, "_NET_WM_DESKTOP",
        False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &val, 1);
    val = 0;
    XChangeProperty(display, window, XInternAtom(display, "_WIN_LAYER",
        False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &val, 1);
    Atom state[4] = {
        XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False),
        XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False),
        XInternAtom(display, "_NET_WM_STATE_SKIP_STICKY", False),
        XInternAtom(display, "_NET_WM_STATE_SKIP_BELOW", False)
    };
    XChangeProperty(display, window, XInternAtom(display, "_NET_WM_STATE",
        False), XA_ATOM, 32, PropModeReplace, (unsigned char *) state, 4);
    long prop[5] = {2, 0, 0, 0, 0};
    XChangeProperty(display, window, XInternAtom(display, "_MOTIF_WM_HINTS",
        False), XInternAtom(display, "_MOTIF_WM_HINTS", False), 32,
        PropModeReplace, (unsigned char *) prop, 5);

    /* show the window with the current time */
    update_time(); draw_time();
    XSelectInput(display, window, ExposureMask);
    XMapWindow(display, window);

    XEvent event;
    fd_set fds;
    struct timeval tv;
    int current_time;

    while (running == 1) {
        /* read all pending x events */
        while (XPending(display) > 0) {
            XNextEvent(display, &event);
            if (event.type == Expose)
                need_update = 1;
        }

        if (need_update == 1)
            draw_time();

        current_time = time(NULL);
        tv.tv_usec = 0;
        tv.tv_sec = 0;

        /* do we need to update the time string or can we wait */
        if (last_update + update_interval <= current_time)
            update_time();
        else
            tv.tv_sec = update_interval - (current_time - last_update);

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        select(fd + 1, &fds, NULL, NULL, &tv);
    }

    return 0;
}

/* the new signal handler */
static void sig_handler(int signal)
{
    running = 0;
}

/* lets cleanup everything at exit */
static void cleanup()
{
    XftColorFree(display, visual, colormap, &color);
    if (draw != NULL)
        XftDrawDestroy(draw);
    if (font != NULL)
        XftFontClose(display, font);

    if (window != None)
        XDestroyWindow(display, window);
    if (display != NULL)
        XCloseDisplay(display);

    if (ferror(stdout) != 0 || fclose(stdout) != 0) {
        fprintf(stderr, "Write error: %s\n", strerror(errno));
        _Exit(EXIT_FAILURE);
    }
    if (ferror(stderr) != 0 || fclose(stderr) != 0)
        _Exit(EXIT_FAILURE);
}

static void parse_cmdline(int argc, char *argv[])
{
    struct option options[] = {
        {"format", required_argument, NULL, 't'},
        {"font", required_argument, NULL, 'f'},
        {"color", required_argument, NULL, 'c'},
        {"position", required_argument, NULL, 'p'},
        {"update", required_argument, NULL, 'u'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0}
    };

    char c;
    int index = 0;

    char *font_name = "sans-9";
    char *color_name = "white";

    while ((c = getopt_long(argc, argv, "t:f:c:p:u:hv", options, &index)) != -1)
        switch (c) {
            case 't':
                time_format = optarg;
                break;
            case 'f':
                font_name = optarg;
                break;
            case 'c':
                color_name = optarg;
                break;
            case 'p':
                sscanf(optarg, "%i,%i", &x, &y);
                break;
            case 'u':
                update_interval = atoi(optarg);
                break;
            case 'v':
                printf(PACKAGE_STRING "\n");
                exit(EXIT_SUCCESS);
            default:
                printf("Usage: thetime [OPTIONS]\n\n"
                    "Options\n"
                    "  -t, --format=FMT   The strftime(3) compatible time "
                    "format\n"
                    "  -f, --font=FONT    The Xft aware font description\n"
                    "  -c, --color=COLOR  The font color\n"
                    "  -p, --position=X,Y Position of the window, negative "
                    "values are treated\n"
                    "                     as starting from opposite\n"
                    "  -u, --update=TIME  Update interval in seconds\n"
                    "\n  -h, --help         Print out this help\n"
                    "  -v, --version      Print the current version\n");
                exit(EXIT_SUCCESS);
        }

    font = XftFontOpenName(display, screen, font_name);
    XftColorAllocName(display, visual, colormap, color_name, &color);
}

/* update the time string */
static void update_time()
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    if (strftime(time_string, 55, time_format, tm) == 0) {
        fprintf(stderr, "Failed to build the time string.\n");
        exit(EXIT_FAILURE);
    }

    last_update = t;
    need_update = 1;
}

/* just update the window and repaint the time string */
static void draw_time()
{
    XGlyphInfo extents;
    XftTextExtents8(display, font, (const FcChar8 *) time_string, strlen(
        time_string), &extents);

    int tmp_width = extents.xOff;
    int tmp_height = font->descent + font->ascent;

    if (width < tmp_width || height < tmp_height) {
        width = tmp_width;
        height = tmp_height;

        int tmp_x = x, tmp_y = y;

        if (x < 0)
            tmp_x = DisplayWidth(display, screen) - abs(x) - width;
        if (y < 0)
            tmp_y = DisplayHeight(display, screen) - abs(y) - height;

        XMoveResizeWindow(display, window, tmp_x, tmp_y, width, height);
        set_background();
    }

    XClearWindow(display, window);
    XftDrawString8(draw, &color, font, 0, height - font->descent,
        (const FcChar8 *)time_string, strlen(time_string));
    XFlush(display);

    need_update = 0;
}

/* set the window background */
static void set_background()
{
    Atom root_id = XInternAtom(display, "_XROOTPMAP_ID", False), type = None;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    int result = XGetWindowProperty(display, root_window, root_id, 0, 1, False,
        XA_PIXMAP, &type, &format, &nitems, &bytes_after, &prop);

    if (result != Success || prop == NULL) {
        fprintf(stderr, "Failed to detect background pixmap.\n");
        exit(EXIT_FAILURE);
    }

    Pixmap root_pixmap = *((Pixmap *) prop);
    XFree(prop);

    Window root, child;
    int x, y;
    unsigned width, height, border_width, depth;

    XGetGeometry(display, window, &root, &x, &y, &width, &height,
        &border_width, &depth);
    XTranslateCoordinates(display, window, root_window, 0, 0, &x, &y, &child);

    Pixmap background = XCreatePixmap(display, root_pixmap, width, height,
        depth);
    XCopyArea(display, root_pixmap, background, DefaultGC(display,
        screen), x, y, width, height, 0, 0);
    XSetWindowBackgroundPixmap(display, window, background);
}


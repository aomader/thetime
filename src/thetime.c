/**
 * thetime -- a tiny X app to show the current time
 * Copyright (C) 2009,2010  Oliver Mader <dotb52@gmail.com>
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
#include <X11/Xresource.h>
#include <X11/Xft/Xft.h>

/* general xlib vars */
static Display *display = NULL;
static int display_width, display_height;
static int fd, screen;
static Window window = None;
static Visual *visual;
static Colormap colormap;
static XftFont *font;
static XftDraw *draw;
static XftColor color;

enum {TOP, BOTTOM, LEFT, RIGHT};

/* runtime settings */
static volatile sig_atomic_t running = 1;
static int x = 20, y = 20, x_orientation = LEFT, y_orientation = TOP;
static int last_x = 0, last_y = 0, last_width = 0, last_height = 0;
static int last_update = 0, need_update = 1, update_interval = 1;
static int need_redraw = 0;
static char time_string[56];
static char *time_format = "%T";

/* prototypes */
static void sig_handler(int signal);
static void cleanup();
static void get_settings(int argc, char *argv[]);
static void update_time();
static void draw_time();
static int rect_overlap(int x, int y, int width, int height);

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
    window = DefaultRootWindow(display);
    display_width = DisplayWidth(display, screen);
    display_height = DisplayHeight(display, screen);
    draw = XftDrawCreate(display, window, visual, colormap);

    /* try to get the runtime settings from cmdline and Xdefaults */
    get_settings(argc, argv);
    
    XEvent event;
    fd_set fds;
    struct timeval tv;
    int current_time;

    update_time();

    XSelectInput(display, window, ExposureMask);
    while (running == 1) {
        /* read all pending x events */
        while (XPending(display) > 0) {
            XNextEvent(display, &event);
            if (event.type == Expose &&
                rect_overlap(event.xexpose.x, event.xexpose.y,
                event.xexpose.width, event.xexpose.height))
            {
                need_redraw = 1;
            }
        }

        if (need_redraw == 1)
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

    /* before we exit we need to clear the window */
    XClearArea(display, window, last_x, last_y, last_width, last_height,
        False);

    if (display != NULL)
        XCloseDisplay(display);

    if (ferror(stdout) != 0 || fclose(stdout) != 0) {
        fprintf(stderr, "Write error: %s\n", strerror(errno));
        _Exit(EXIT_FAILURE);
    }
    if (ferror(stderr) != 0 || fclose(stderr) != 0)
        _Exit(EXIT_FAILURE);
}

/* read settings from Xresources and cmd line */
static void get_settings(int argc, char *argv[])
{
    char *font_name = "sans-9";
    char *color_name = "white";
    char *position = "20,20";
    
    char *value;
    if ((value = XGetDefault(display, "thetime", "format")) != NULL)
        time_format = value;
    if ((value = XGetDefault(display, "thetime", "font")) != NULL)
        font_name = value;
    if ((value = XGetDefault(display, "thetime", "color")) != NULL)
        color_name = value;
    if ((value = XGetDefault(display, "thetime", "position")) != NULL)
        position = value;
    if ((value = XGetDefault(display, "thetime", "update")) != NULL)
        update_interval = atoi(value);

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

    while ((c = getopt_long(argc, argv, "t:f:c:p:u:hv", options, &index))
        != -1)
    {
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
                position = optarg;
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
                    "  -t, --format=FORMAT    The strftime(3) compatible "
                    "time format\n"
                    "  -f, --font=FONT        The Xft aware font "
                    "description\n"
                    "  -c, --color=COLOR      The font color\n"
                    "  -p, --position=X,Y     Position of the window, "
                    "negative values are treated\n"
                    "                         as starting from opposite\n"
                    "  -u, --update=INTERVAL  Update screen each INTERVAL"
                    " seconds\n"
                    "\n  -h, --help             Print out this help\n"
                    "  -v, --version          Print the current version\n");
                exit(EXIT_SUCCESS);
        }
    }

    font = XftFontOpenName(display, screen, font_name);
    XftColorAllocName(display, visual, colormap, color_name, &color);

    sscanf(position, "%i,%i", &x, &y);

    if (position[0] == '-')
        x_orientation = RIGHT;
    if (*(strchr(position, ',') + 1) == '-')
        y_orientation = BOTTOM;
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
    need_redraw = need_update = 1;
}

/* just update the window and repaint the time string */
static void draw_time()
{
    int tmp_x = last_x, tmp_y = last_y;
    int tmp_width = last_width, tmp_height = last_height;

    if (need_update == 1) {
        XGlyphInfo extents;
        XftTextExtents8(display, font, (const FcChar8 *) time_string, strlen(
            time_string), &extents);

        tmp_width = extents.xOff;
        tmp_height = font->descent + font->ascent;
        tmp_x = (x_orientation == RIGHT) ? display_width - abs(x) - tmp_width
            : x;
        tmp_y = (y_orientation == BOTTOM) ? display_height - abs(y) -
            tmp_height : y;
    }

    XClearArea(display, window, last_x, last_y, last_width, last_height,
        False);
    XftDrawStringUtf8(draw, &color, font, tmp_x, tmp_y + tmp_height -
        font->descent, (const FcChar8 *) time_string, strlen(time_string));
    XFlush(display);

    if (need_update == 1) {
        last_width = tmp_width;
        last_height = tmp_height;
        last_x = tmp_x;
        last_y = tmp_y;
    }

    need_redraw = need_update = 0;
}

/* lets check if the expose rect overlaps the time string */
static int rect_overlap(int x, int y, int width, int height)
{
    int a1_x = last_x, a2_x = last_x + last_width;
    int a1_y = last_y, a2_y = last_y + last_height;

    int b1_x = x, b2_x = x + width;
    int b1_y = y, b2_y = y + height;

    if (b1_x > a2_x || b2_x < a1_x || b1_y > a2_y || b2_y < a1_y)
        return 0;

    return 1;
}

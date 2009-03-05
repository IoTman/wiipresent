/*
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Library General Public License as published by
the Free Software Foundation; version 2 only

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Library General Public License for more details.

You should have received a copy of the GNU Library General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
Copyright 2009 Dag Wieers <dag@wieers.com>
*/

// $Id$

#define _GNU_SOURCE

#include <getopt.h>
#include <math.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/XF86keysym.h>
#include <X11/keysym.h>

#include "wiimote_api.h"

static char NAME[] = "wiipresent";
static char VERSION[] = "0.7";

static char *displayname = NULL;
static Display *display = NULL;
static Window window = 0;
wiimote_t wmote;

int verbose = 0;

// Screensaver variables
int timeout_return = 0;
int interval_return = 0;
int prefer_blanking_return = 0;
int allow_exposures_return = 0;

static void XFakeKeycode(int keycode, int modifiers){
    if ( modifiers & ControlMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), True, 0);

    if ( modifiers & Mod1Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), True, 0);

    if ( modifiers & Mod2Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_R), True, 0);

    if ( modifiers & ShiftMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L), True, 0);

    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keycode), True, 0);

    XSync(display, False);

    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keycode), False, 0);

    if ( modifiers & ShiftMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L), False, 0);

    if ( modifiers & Mod2Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_R), False, 0);

    if ( modifiers & Mod1Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), False, 0);

    if ( modifiers & ControlMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), False, 0);
}

void XMovePointer(Display *display, int xpos, int ypos, int relative) {
    if (relative)
        XTestFakeRelativeMotionEvent(display, xpos, ypos, 0);
    else
        XTestFakeMotionEvent(display, -1, xpos, ypos, 0);
}

void XClickMouse(Display *display, int button, int release) {
    XTestFakeButtonEvent(display, button, release, 0);
    XSync(display, False);
}

Status XFetchProperty (register Display *display, Window window, int property, char **name) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long leftover;
    unsigned char *data = NULL;
    if (XGetWindowProperty(display, window, property, 0L, (long) BUFSIZ,
            False, XA_STRING, &actual_type, &actual_format,
            &nitems, &leftover, &data) != Success) {
        *name = NULL;
        return 0;
    }
    if ( (actual_type == XA_STRING) && (actual_format == 8) ) {
        // Cut of paths if any
        *name = basename((char *) data);
        return 1;
    }
    if (data) XFree((char *) data);
    *name = NULL;
    return 0;
}

Status XQueryCommand(Display *display, Window window, char **name) {
    Window root_window;
    Window parent_window;
    Window *children_window;
    unsigned int nchildrens;

    // Prevent BadWindow
    if (window < 0xff ) return 0;

    if (verbose >= 3) fprintf(stderr, "Working with window (0x%x).\n", (unsigned int) window);

    XClassHint xclasshint;
    if (XGetClassHint(display, window, &xclasshint) != 0) {
        *name = xclasshint.res_class;
        XFree(xclasshint.res_name);
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%x) using XGetClassHint.\n", *name, (unsigned int) window);
    } else if (XFetchProperty(display, window, XA_WM_COMMAND, name) != 0) {
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%x) using XA_WM_COMMAND.\n", *name, (unsigned int) window);
    } else if (window != window - window % 0x100000 + 1 && XQueryCommand(display, window - window % 0x100000 + 1, name) != 0) {
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%x) using guessed parent window.\n", *name, (unsigned int) (window - window % 0x100000 + 1));
    } else if (XQueryTree(display, window, &root_window, &parent_window, &children_window, &nchildrens) != 0) {
        if (parent_window) {
            if (XQueryCommand(display, parent_window, name) != 0) {
                if (verbose >= 3) fprintf(stderr, "Found application %s (0x%x) using real parent window.\n", *name, (unsigned int) parent_window);
            } else if (XFetchProperty(display, window, XA_WM_NAME, name) != 0) {
                if (verbose >= 3) fprintf(stderr, "Found application %s (0x%x) using XA_WM_NAME.\n", *name, (unsigned int) window);
            } else {
                 return 0;
            }
        }
    }
    return 1;
}

void exit_clean(int sig) {
    wiimote_disconnect(&wmote);
    XSetScreenSaver(display, timeout_return, interval_return, prefer_blanking_return, allow_exposures_return);
    switch(sig) {
        case(0):
        case(2):
            exit(0);
        default:
            printf("Exiting on signal %d.\n", sig);
            exit(sig);
    }
}

void rumble(wiimote_t *wmote, int msecs) {
    wiimote_update(wmote);
    wmote->rumble = 1;
    wiimote_update(wmote);
    usleep(msecs * 1000);
    wmote->rumble = 0;
}

// Is this a valid point ?
int valid_point(wiimote_ir_t *point) {
    if (point == NULL)
        return 0;
    if (point->size == 0 || point->size == 15 || point->x == 0 || point->x == 1791 || point->y == 0 || point->y == 1791)
        return 0;
    return 1;
}

// This function returns the largest point not already discovered
wiimote_ir_t *search_newpoint(wiimote_t *wmote, wiimote_ir_t *other) {
    wiimote_ir_t *new = &wmote->ir1;
    wiimote_ir_t *maybe = &wmote->ir2;
    if (valid_point(maybe) && maybe != other && maybe->size < new->size) {
        new = maybe;
    }
    maybe = &wmote->ir3;
    if (valid_point(maybe) && maybe != other && maybe->size < new->size) {
        new = maybe;
    }
    maybe = &wmote->ir4;
    if (valid_point(maybe) && maybe != other && maybe->size < new->size) {
        new = maybe;
    }
    return new;
}

int main(int argc, char **argv) {
    int length = 0;
    char *btaddress = NULL;
    int infrared = False;
    int tilt = True;
    wmote = (wiimote_t) WIIMOTE_INIT;

    int c;

    // Make stdout unbuffered
    setvbuf(stdout, NULL, _IONBF, 0);

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"bluetooth", 1, 0, 'b'},
            {"display", 1, 0, 'd'},
            {"help", 0, 0, 'h'},
            {"ir", 0, 0, 'i'},
            {"infrared", 0, 0, 'i'},
            {"length", 1, 0, 'l'},
            {"tilt", 0, 0, 't'},
            {"verbose", 0, 0, 'v'},
            {"version", 0, 0, 'V'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "b:d:hil:tvV", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'b':
                btaddress = optarg;
                continue;
            case 'd':
                displayname = optarg;
                continue;
            case 'h':
                printf("Nintendo Wiimote presentation controller\n\
\n\
%s options:\n\
  -b, --bluetooth=btaddress      wiimote bluetooth address (use hcitool scan)\n\
  -d, --display=name             X display to use\n\
  -i, --infrared                 use infrared sensor to move mouse pointer\n\
  -l, --length=minutes           presentation length in minutes\n\
  -t, --tilt                     use tilt sensors to move mouse pointer\n\
\n\
  -h, --help                     display this help and exit\n\
  -v, --verbose                  increase verbosity\n\
      --version                  output version information and exit\n\
\n\
Report bugs to <dag@wieers.com>.\n", NAME);
                exit(0);
            case 'i':
                infrared = True;
                tilt = False;
                continue;
            case 'l':
                length = atoi(optarg) * 60;
                continue;
            case 't':
                tilt = True;
                infrared = False;
                continue;
            case 'v':
                verbose += 1;
                continue;
            case 'V':
                printf("%s %s\n\
Copyright (C) 2009 Dag Wieërs\n\
This is open source software.  You may redistribute copies of it under the terms of\n\
the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n\
There is NO WARRANTY, to the extent permitted by law.\n\
\n\
Written by Dag Wieers <dag@wieers.com>.\n", NAME, VERSION);
                exit(0);
            default:
                printf ("?? getopt returned character code 0%o ??\n", c);
        }

        if (optind < argc) {
            printf ("non-option ARGV-elements: ");
            while (optind < argc)
                printf ("%s ", argv[optind++]);
            printf ("\n");
        }
    }

    // Wait for 1+2
    if (btaddress == NULL) {
//        printf("Please press 1+2 on a wiimote in the viscinity...");
//        wiimote_connect(&wmote, btaddress);
        printf("Sorry, you need to provide a bluetooth address using -b/--bluetooth.\n");
        exit(1);
    } else {
        printf("Please press 1+2 on the wiimote with address %s...", btaddress);
        wiimote_connect(&wmote, btaddress);
        printf("\n");
    }

    signal(SIGINT, exit_clean);
    signal(SIGHUP, exit_clean);
    signal(SIGQUIT, exit_clean);

    if (tilt)
        fprintf(stderr, "Mouse movement controlled by tilting wiimote.\n");
    else if (infrared)
        fprintf(stderr, "Mouse movement controlled by infrared reception emitted from sensor bar.\n");
    else
        fprintf(stderr, "Mouse movement disabled.\n");

    if (length) fprintf(stderr, "Presentation length is %dmin divided in 5 slots of %dmin.\n", length/60, length/60/5);

    // Obtain the X11 display.
    if (displayname == NULL)
        displayname = getenv("DISPLAY");

    if (displayname == NULL)
        displayname = ":0.0";

    display = XOpenDisplay(displayname);
    if (display == NULL) {
        fprintf(stderr, "%s: can't open display `%s'.\n", NAME, displayname);
        return -1;
    }

    // Disable screensaver
    XGetScreenSaver(display, &timeout_return, &interval_return, &prefer_blanking_return, &allow_exposures_return);
    XSetScreenSaver(display, 0, 0, 1, 0);

    // Get the root window for the current display.
    int revert;

    time_t start = 0, now = 0, duration = 0;
    int phase = 0, oldphase = 0;
    uint16_t keys = 0;

    int x = 0, y = 0;
    int prev1x = 0, prev1y = 0;
    int prev2x = 0, prev2y = 0;
    int dots = 0;
    wiimote_ir_t *point1 = &wmote.ir1, *point2 = &wmote.ir2;

    int oldbattery = 0;
    Window oldwindow = window;
    int playertoggle = False;
    int fullscreentoggle = False;
    int screensavertoggle = False;
    int mouse = False;
    int leftmousebutton = False;
    int rightmousebutton = False;

    char *name;
    XGetInputFocus(display, &window, &revert);
    XQueryCommand(display, window, &name);
    oldwindow = window;

    rumble(&wmote, 200);

    start = time(NULL);

    while (wiimote_is_open(&wmote)) {

        // Find the window which has the current keyboard focus.
        XGetInputFocus(display, &window, &revert);

        // Handle focus changes
        if (window != oldwindow) {
            if (name) XFree(name);
            if (XQueryCommand(display, window, &name) != 0) {
                if (verbose >= 2) fprintf(stderr, "Focus on application %s (0x%x)\n", name, (unsigned int) window);
            } else {
                name = strdup("(unknown)");
                fprintf(stderr, "ERROR: Unable to find application name for window 0x%x\n", (unsigned int) window);
            }
            oldwindow = window;
        }

        if (wiimote_pending(&wmote) == 0) {
            usleep(10000);
        }

        if (wiimote_update(&wmote) < 0) {
            printf("Lost connection.");
            exit_clean(0);
        }

        // Check battery change
        if (wmote.battery != oldbattery) {
            if (wmote.battery < 5)
                printf("Battery low (%d%%), please replace batteries !\n", wmote.battery);
            else
                printf("Battery level now is %d%%.\n", wmote.battery);
            oldbattery = wmote.battery;
        }

        // Change leds only when phase changes
        if (length) {
            now = time(NULL);
            duration = now - start;
            phase = (int) floorf( ( (float) duration * 5.0 / (float) length)) % 5;
            if (phase != oldphase) {
                printf("%ld minutes passed, %ld minutes left. (phase=%d)\n", duration / 60, (length - duration) / 60, phase);
                // Shift the leds
                wmote.led.bits = pow(2, phase) - 1;

                // Rumble slightly longer at the end (exponentially)
                rumble(&wmote, 100 * exp(phase + 1) / 10);

                switch (phase)  {
                    case 0:
                        printf("Sorry, time is up !\n");
                        break;
                    case 4:
                        printf("Hurry up ! Maybe questions ?\n");
                        break;
                }
                oldphase = phase;
            }
        }

//        printf("%f - %f - %f - %ld - %ld - %ld - %d\n", ((float) duration * 5.0 / (float) length), (float) duration, (float) length, start, now, duration, phase);

        // Inside the mouse functionality
        if (wmote.keys.b) {
            if (! mouse) {
                if (verbose >= 3) fprintf(stderr, "Mouse enabled.\n");
                mouse = ! mouse;

                if (tilt) {
                    wmote.mode.acc = 1;
                } else if (infrared) {
                    wmote.mode.ir = 1;
                }
            }

            // Tilt method
            if (tilt) {
                XMovePointer(display, wmote.tilt.x / 4, wmote.tilt.y / 4, 1);

            // Infrared method
            } else if (infrared) {

                if (!valid_point(point1) || (point1 == point2)) {
                    point1 = search_newpoint(&wmote, point2);
                } else {
                    fprintf(stderr, "Point 1 is valid %4d %4d %2d\n", point1->x, point1->y, point1->size);
                }

                if (!valid_point(point2) || (point1 == point2)) {
                    point2 = search_newpoint(&wmote, point1);
                } else {
                    fprintf(stderr, "Point 2 is valid %4d %4d %2d\n", point2->x, point2->y, point2->size);
                }

                if (valid_point(point1) && ! valid_point(point2))
                    XMovePointer(display, 1280 * (prev1x - point1->x) / 1791,
                                         -800 * (prev1y - point1->y) / 1791, 1);
                else if (valid_point(point1) && ! valid_point(point2))
                    XMovePointer(display, 1280 * (prev2x - point2->x) / 1791,
                                         -800 * (prev2y - point2->y) / 1791, 1);
                else if (point1 == point2)
                    XMovePointer(display, 1280 * (prev1x - point1->x) / 1791,
                                         -800 * (prev1y - point1->y) / 1791, 1);
                else
                    XMovePointer(display, 1280 * (prev1x - point1->x > prev2x - point2->x ? prev2x - point2->x : prev1x - point1->x) / 1791,
                                         -800 * (prev1y - point1->y > prev2y - point2->y ? prev2y - point2->y : prev1y - point1->y) / 1791, 1);

/*
                prev1x = point1->x;
                prev1y = point1->y;
                prev2x = point2->x;
                prev2y = point2->y;

                dots = (wmote.ir1.x !=0 && wmote.ir1.x != 1791 ? 1 : 0) +
                       (wmote.ir2.x !=0 && wmote.ir2.x != 1791 ? 1 : 0) +
                       (wmote.ir3.x !=0 && wmote.ir3.x != 1791 ? 1 : 0) +
                       (wmote.ir4.x !=0 && wmote.ir4.x != 1791 ? 1 : 0);
                if (dots > 0) {
                    x = ( (wmote.ir1.x !=0 && wmote.ir1.x != 1791 ? wmote.ir1.x : 0) +
                          (wmote.ir2.x !=0 && wmote.ir2.x != 1791 ? wmote.ir2.x : 0) +
                          (wmote.ir3.x !=0 && wmote.ir3.x != 1791 ? wmote.ir3.x : 0) +
                          (wmote.ir4.x !=0 && wmote.ir4.x != 1791 ? wmote.ir4.x : 0) ) / dots;
                    y = ( (wmote.ir1.x !=0 && wmote.ir1.x != 1791 ? wmote.ir1.y : 0) +
                          (wmote.ir2.x !=0 && wmote.ir2.x != 1791 ? wmote.ir2.y : 0) +
                          (wmote.ir3.x !=0 && wmote.ir3.x != 1791 ? wmote.ir3.y : 0) +
                          (wmote.ir4.x !=0 && wmote.ir4.x != 1791 ? wmote.ir4.y : 0) ) / dots;
                    XMovePointer(display, 1280 * (1791 - x) / 1791, 800 * y / 1791, 0);
                } else {
                    x = 0;
                    y = 0;
                }
*/
                if (verbose >= 2) fprintf(stderr, "%d: ( %4d , %4d ) - [ %4d, %4d, %4d, %4d ] [ %4d, %4d, %4d, %4d ] [%2d, %2d, %2d, %2d ]\n", dots, x, y, wmote.ir1.x, wmote.ir2.x,wmote.ir3.x, wmote.ir4.x, wmote.ir1.y, wmote.ir2.y, wmote.ir3.y, wmote.ir4.y, wmote.ir1.size, wmote.ir2.size, wmote.ir3.size, wmote.ir4.size);
            }

            // Block repeating keys
            if (keys == wmote.keys.bits) {
                continue;
            }

            // Left mouse button events
            if (wmote.keys.minus || wmote.keys.a) {
                if (! leftmousebutton) {
                    if (verbose >= 3) fprintf(stderr, "Mouse left button pressed.\n");
                    XClickMouse(display, 1, 1);
                    leftmousebutton = ! leftmousebutton;
                }
            } else {
                if (leftmousebutton) {
                    if (verbose >= 3) fprintf(stderr, "Mouse left button released.\n");
                    XClickMouse(display, 1, 0);
                    leftmousebutton = ! leftmousebutton;
                }
            }

            // Right mouse button events
            if (wmote.keys.plus) {
                if (! rightmousebutton) {
                    if (verbose >= 3) fprintf(stderr, "Mouse right button pressed.\n");
                    XClickMouse(display, 3, 1);
                    rightmousebutton = ! rightmousebutton;
                }
            } else {
                if (rightmousebutton) {
                    if (verbose >= 3) fprintf(stderr, "Mouse right button released.\n");
                    XClickMouse(display, 3, 0);
                    rightmousebutton = ! rightmousebutton;
                }
            }

            if (wmote.keys.up) {
                if (strcasestr(name, "firefox") == name) {          // Scroll Up
                    XFakeKeycode(XK_Page_Up, 0);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_Page_Up, 0);
                }
            }

            if (wmote.keys.down) {
                if (strcasestr(name, "firefox") == name) {          // Scroll Up
                    XFakeKeycode(XK_Page_Down, 0);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_Page_Down, 0);
                }
            }
        } else {
            if (mouse) {
                if (verbose >= 3) fprintf(stderr, "Mouse disabled.\n");
                mouse = ! mouse;

                wmote.mode.acc = 0;
                wmote.mode.ir = 0;
            }


            // Block repeating keys
            if (keys == wmote.keys.bits) {
                continue;
            }

            // Goto to previous workspace
            if (wmote.keys.plus) {
                XFakeKeycode(XK_Right, ControlMask | Mod1Mask);
            }

            // Goto to next workspace
            if (wmote.keys.minus) {
                XFakeKeycode(XK_Left, ControlMask | Mod1Mask);
            }

            // Go home/back
            if (wmote.keys.home) {
                if (strcasestr(name, "firefox") == name) {          // Enter
                    XFakeKeycode(XK_Home, 0);
                } else if (strcasestr(name, "yelp") == name) {
                    XFakeKeycode(XK_Home, 0);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_Home, 0);
                } else if (strcasestr(name, "nautilus") == name) {
                    XFakeKeycode(XK_BackSpace, ShiftMask);
                } else if (strcasestr(name, "openoffice") == name ||
                           strcasestr(name, "soffice") == name) {
                    XFakeKeycode(XK_Home, 0);
                } else {
                    if (verbose)
                        fprintf(stderr, "No home-key support for application %s.\n", name);
                }
            }

            if (wmote.keys.a) {
                if (strcasestr(name, "firefox") == name) {          // Enter
                    XFakeKeycode(XK_Return, 0);
                } else if (strcasestr(name, "yelp") == name) {
                    XFakeKeycode(XK_Return, 0);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_Return, 0);
                } else if (strcasestr(name, "openoffice") == name ||
                           strcasestr(name, "soffice") == name) {
                    XFakeKeycode(XK_Page_Down, 0);
                } else if (strcasestr(name, "rhythmbox") == name) { // Play/Pause
                    XFakeKeycode(XK_space, ControlMask); 
                } else if (strcasestr(name, "mplayer") == name) {
                    XFakeKeycode(XK_p, 0);
                } else if (strcasestr(name, "xine") == name) {
                    XFakeKeycode(XK_space, 0);
                } else if (strcasestr(name, "tvtime") == name) {    // Change screen ratio
                    XFakeKeycode(XK_a, 0);
                } else if (strcasestr(name, "qiv") == name) {       // Maximize
                    XFakeKeycode(XK_m, 0);
                } else if (strcasestr(name, "nautilus") == name) {
                    XFakeKeycode(XK_Return, ShiftMask);
                } else {
                    if (verbose)
                        fprintf(stderr, "No A-key support for application %s.\n", name);
                }
                playertoggle = ! playertoggle;
            }

            if (wmote.keys.one) {
                if (strcasestr(name, "firefox") == name) {          // Fullscreen
                    XFakeKeycode(XK_F11, 0);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_F11, 0);
                } else if (strcasestr(name, "evince") == name) {
                    XFakeKeycode(XK_F5, 0);
                } else if (strcasestr(name, "openoffice") == name ||
                           strcasestr(name, "soffice") == name) {
                    if (fullscreentoggle)
                        XFakeKeycode(XK_Escape, 0);
                    else
                        XFakeKeycode(XK_F9, 0);
                } else if (strcasestr(name, "gqview") == name) {
                    XFakeKeycode(XK_F, 0);
                } else if (strcasestr(name, "qiv") == name) {
                    XFakeKeycode(XK_f, 0);
                } else if (strcasestr(name, "eog") == name) {
                    XFakeKeycode(XK_F11, 0);
                } else if (strcasestr(name, "xpdf") == name) {
                    XFakeKeycode(XK_F, Mod1Mask);
                } else if (strcasestr(name, "acroread") == name) {
                    XFakeKeycode(XK_L, ControlMask);
                } else if (strcasestr(name, "rhythmbox") == name) {
                    XFakeKeycode(XK_F11, 0);
                } else if (strcasestr(name, "tvtime") == name) {
                    XFakeKeycode(XK_f, 0);
                } else if (strcasestr(name, "mplayer") == name) {
                    XFakeKeycode(XK_f, 0);
                } else if (strcasestr(name, "vlc") == name) {
                    XFakeKeycode(XK_f, 0);
                } else if (strcasestr(name, "xine") == name) {
                    XFakeKeycode(XK_f, 0);
                } else if (verbose) {
                    fprintf(stderr, "No one-key support for application %s.\n", name);
                }
                fullscreentoggle = ! fullscreentoggle;
            }

            if (wmote.keys.two) {

                // Mute audio
                if (strcasestr(name, "mplayer") == name) {
                    XFakeKeycode(XK_m, 0);
                } else if (strcasestr(name, "xine") == name) {
                    XFakeKeycode(XK_m, ControlMask);
                } else {
                    XFakeKeycode(XF86XK_AudioMute, 0);
                }

                // Blank screen
                if (screensavertoggle) {
                    XForceScreenSaver(display, ScreenSaverReset);
                } else {
                    XActivateScreenSaver(display);
                }

                screensavertoggle = ! screensavertoggle;
            }

            if (wmote.keys.up) {
                if (strcasestr(name, "firefox") == name) {          // Scroll Up
                    XFakeKeycode(XK_Tab, ShiftMask);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_Up, ControlMask);
                } else if (strcasestr(name, "yelp") == name) {
                    XFakeKeycode(XK_Tab, ShiftMask);
                } else if (strcasestr(name, "pidgin") == name) {
                    XFakeKeycode(XK_Page_Up, 0);
                } else if (strcasestr(name, "rhythmbox") == name) { // Volume Up
                    XFakeKeycode(XF86XK_AudioRaiseVolume, 0);
//                    XFakeKeycode(XK_Up, ControlMask);
                } else if (strcasestr(name, "tvtime") == name) {
                    XFakeKeycode(XK_KP_Add, 0);
                } else if (strcasestr(name, "vlc") == name) {
                    XFakeKeycode(XK_Up, ControlMask);
                } else if (strcasestr(name, "xine") == name) {
                    XFakeKeycode(XK_V, ShiftMask);
                } else if (strcasestr(name, "mplayer") == name) {
                    XFakeKeycode(XK_0, ShiftMask);
                // FIXME: This does not work
                } else if (strcasestr(name, "gqview") == name) {      // Rotate Clockwise
                    XFakeKeycode(XK_bracketright, 0);
                } else if (strcasestr(name, "qiv") == name) {
                    XFakeKeycode(XK_k, 0);
                } else if (strcasestr(name, "eog") == name) {
                    XFakeKeycode(XK_r, ControlMask);
                } else if (strcasestr(name, "nautilus") == name) {
                    XFakeKeycode(XK_Up, 0);
                } else {
                    if (verbose) fprintf(stderr, "No up-key for application %s.\n", name);
                }
            }

            if (wmote.keys.down) {
                if (strcasestr(name, "firefox") == name) {          // Scroll Down
                    XFakeKeycode(XK_Tab, 0);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_Down, ControlMask);
                } else if (strcasestr(name, "yelp") == name) {
                    XFakeKeycode(XK_Tab, 0);
                } else if (strcasestr(name, "pidgin") == name) {
                    XFakeKeycode(XK_Page_Down, 0);
                } else if (strcasestr(name, "rhythmbox") == name) {
                    XFakeKeycode(XF86XK_AudioLowerVolume, 0);       // Volume Down
//                    XFakeKeycode(XK_Down, ControlMask);
                } else if (strcasestr(name, "tvtime") == name) {
                    XFakeKeycode(XK_KP_Subtract, 0);
                } else if (strcasestr(name, "vlc") == name) {
                    XFakeKeycode(XK_Down, ControlMask);
                } else if (strcasestr(name, "xine") == name) {
                    XFakeKeycode(XK_v, 0);
                } else if (strcasestr(name, "mplayer") == name) {
                    XFakeKeycode(XK_9, ShiftMask);
                // FIXME: This does not work
                } else if (strcasestr(name, "gqview") == name) {    // Rotate Counter Clockwise
                    XFakeKeycode(XK_bracketleft, 0);
                } else if (strcasestr(name, "qiv") == name) {
                    XFakeKeycode(XK_l, 0);
                // FIXME: No key in eog for rotating counter clockwise ?
                } else if (strcasestr(name, "eog") == name) {
                    XFakeKeycode(XK_r, ShiftMask | ControlMask);
                } else if (strcasestr(name, "nautilus") == name) {
                    XFakeKeycode(XK_Down, 0);
                } else {
                    if (verbose) fprintf(stderr, "No down-key support for application %s.\n", name);
                }
            }

            if (wmote.keys.right) {
                if (strcasestr(name, "firefox") == name) {              // Next Tab
                    XFakeKeycode(XK_Page_Down, ControlMask);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_F6, ControlMask);
                } else if (strcasestr(name, "yelp") == name) {
                    XFakeKeycode(XK_Right, Mod1Mask);
                } else if (strcasestr(name, "pidgin") == name) {
                    XFakeKeycode(XK_Tab, ControlMask);
                } else if (strcasestr(name, "evince") == name) {        // Next Slide
                    XFakeKeycode(XK_Page_Down, 0);
                } else if (strcasestr(name, "openoffice") == name ||
                           strcasestr(name, "soffice") == name) {
                    XFakeKeycode(XK_Page_Down, 0);
                } else if (strcasestr(name, "gqview") == name) {
                    XFakeKeycode(XK_Page_Down, 0);
                } else if (strcasestr(name, "qiv") == name) {
                    XFakeKeycode(XK_space, 0);
                } else if (strcasestr(name, "eog") == name) {
                    XFakeKeycode(XK_Right, 0);
                } else if (strcasestr(name, "xpdf") == name) {
                    XFakeKeycode(XK_n, 0);
                } else if (strcasestr(name, "acroread") == name) {
                    XFakeKeycode(XK_Page_Down, 0);
                } else if (strcasestr(name, "rhythmbox") == name) {     // Next Song
                    XFakeKeycode(XK_Right, Mod1Mask);
                } else if (strcasestr(name, "tvtime") == name) {        // Next Channel
                    XFakeKeycode(XK_Up, 0);
                } else if (strcasestr(name, "vlc") == name) {           // Skip Forward
                    XFakeKeycode(XK_Right, Mod1Mask);
                } else if (strcasestr(name, "mplayer") == name) {
                    XFakeKeycode(XK_Right, 0);
                } else if (strcasestr(name, "xine") == name) {
                    XFakeKeycode(XK_Right, ControlMask);
                } else if (strcasestr(name, "nautilus") == name) {
                    XFakeKeycode(XK_Right, 0);
                } else {
                    if (verbose) fprintf(stderr, "No right-key support for application %s.\n", name);
                }
            }

            if (wmote.keys.left) {
                if (strcasestr(name, "firefox") == name) {              // Previous Tab
                    XFakeKeycode(XK_Page_Up, ControlMask);
                } else if (strcasestr(name, "opera") == name) {
                    XFakeKeycode(XK_F6, ControlMask | ShiftMask);
                } else if (strcasestr(name, "yelp") == name) {
                    XFakeKeycode(XK_Left, Mod1Mask);
                } else if (strcasestr(name, "pidgin") == name) {
                    XFakeKeycode(XK_Tab, ControlMask | ShiftMask);
                } else if (strcasestr(name, "evince") == name) {        // Previous Slide
                    XFakeKeycode(XK_Page_Up, 0);
                } else if (strcasestr(name, "openoffice") == name ||
                           strcasestr(name, "soffice") == name) {
                    XFakeKeycode(XK_Page_Up, 0);
                } else if (strcasestr(name, "gqview") == name) {
                    XFakeKeycode(XK_Page_Up, 0);
                } else if (strcasestr(name, "qiv") == name) {
                    XFakeKeycode(XK_BackSpace, 0);
                } else if (strcasestr(name, "eog") == name) {
                    XFakeKeycode(XK_Left, 0);
                } else if (strcasestr(name, "xpdf") == name) {
                    XFakeKeycode(XK_p, 0);
                } else if (strcasestr(name, "acroread") == name) {
                    XFakeKeycode(XK_Page_Up, 0);
                } else if (strcasestr(name, "rhythmbox") == name) {    // Previous Song
                    XFakeKeycode(XK_Left, Mod1Mask);
                } else if (strcasestr(name, "tvtime") == name) {       // Previous Channel
                    XFakeKeycode(XK_Down, 0);
                } else if (strcasestr(name, "vlc") == name) {          // Skip Backward
                    XFakeKeycode(XK_Left, Mod1Mask);
                } else if (strcasestr(name, "mplayer") == name) {
                    XFakeKeycode(XK_Left, 0);
                } else if (strcasestr(name, "xine") == name) {
                    XFakeKeycode(XK_Left, ControlMask);
                } else if (strcasestr(name, "nautilus") == name) {
                    XFakeKeycode(XK_Left, 0);
                } else {
                    if (verbose) fprintf(stderr, "No left-key support for application %s.\n", name);
                }
            }

        }

        // Save the keys state for next run
        keys = wmote.keys.bits;
    }
    XCloseDisplay(display);

    return 0;
}

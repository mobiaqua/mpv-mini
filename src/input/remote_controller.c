/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <errno.h>
#include "common/common.h"
#include "common/msg.h"
#include "input.h"
#include "input/keycodes.h"

static int thread_exit_request;

enum remote_type {
    REMOTE_UNKNOWN = 0,
    REMOTE_PS3_BD = 1,
    REMOTE_SATECHI_R2 = 2
};

static enum remote_type remote;

#define INVALID_KEY -1

typedef struct {
    int linux_keycode;
    int mp_keycode;
} mapping;

static const mapping ps3remote_mapping[] = {
  { KEY_ESC,           'q'               },
//  { KEY_1,             '1'              },
//  { KEY_2,             '2'              },
//  { KEY_3,             '3'              },
//  { KEY_4,             '4'              },
//  { KEY_5,             '5'              },
//  { KEY_6,             '6'              },
//  { KEY_7,             '7'              },
//  { KEY_8,             '8'              },
//  { KEY_9,             '9'              },
//  { KEY_0,             '0'              },
  { KEY_ENTER,         ' '              },
  { KEY_UP,            MP_KEY_UP        },
  { KEY_LEFT,          MP_KEY_LEFT      },
  { KEY_RIGHT,         MP_KEY_RIGHT     },
  { KEY_DOWN,          MP_KEY_DOWN      },
  { KEY_PAUSE,         ' '              },
  { KEY_STOP,          'q'              },
//  { KEY_MENU,          KEY_MENU         },
//  { KEY_BACK,          KEY_BACK         },
//  { KEY_FORWARD,       KEY_FORWARD      },
//  { KEY_EJECTCD,       KEY_EJECTCD      },
//  { KEY_REWIND,        KEY_REWIND       },
//  { KEY_HOMEPAGE,      KEY_HOMEPAGE     },
  { KEY_PLAY,          ' '              },
//  { BTN_0,             BTN_0            },
//  { BTN_TL,            BTN_TL           },
//  { BTN_TR,            BTN_TR           },
//  { BTN_TL2,           BTN_TL2          },
//  { BTN_TR2,           BTN_TR2          },
//  { BTN_START,         BTN_START        },
//  { BTN_THUMBL,        BTN_THUMBL       },
//  { BTN_THUMBR,        BTN_THUMBR       },
//  { KEY_SELECT,        KEY_SELECT       },
//  { KEY_CLEAR,         KEY_CLEAR        },
//  { KEY_OPTION,        KEY_OPTION       },
//  { KEY_INFO,          KEY_INFO         },
//  { KEY_TIME,          KEY_TIME         },
//  { KEY_SUBTITLE,      KEY_SUBTITLE     },
//  { KEY_ANGLE,         KEY_ANGLE        },
//  { KEY_SCREEN,        KEY_SCREEN       },
//  { KEY_AUDIO,         KEY_AUDIO        },
//  { KEY_RED,           KEY_RED          },
//  { KEY_GREEN,         KEY_GREEN        },
//  { KEY_YELLOW,        KEY_YELLOW       },
//  { KEY_BLUE,          KEY_BLUE         },
//  { KEY_NEXT,          KEY_NEXT         },
//  { KEY_PREVIOUS,      KEY_PREVIOUS     },
//  { KEY_FRAMEBACK,     KEY_FRAMEBACK    },
//  { KEY_FRAMEFORWARD,  KEY_FRAMEFORWARD },
//  { KEY_CONTEXT_MENU,  KEY_CONTEXT_MENU },
};

static const mapping r2remote_mapping[] = {
  { KEY_HOMEPAGE,        'q'              },
  { KEY_VOLUMEUP,        MP_KEY_UP        },
  { KEY_PREVIOUSSONG,    MP_KEY_LEFT      },
  { KEY_NEXTSONG,        MP_KEY_RIGHT     },
  { KEY_VOLUMEDOWN,      MP_KEY_DOWN      },
  { KEY_PLAYPAUSE,       ' '              },
  { KEY_MUTE,            'm'              },
  { KEY_EJECTCD,         'j'              },
};

static int lookup_button_mp_key(struct input_event *ev)
{
    int i;

    switch (remote)
    {
        case REMOTE_PS3_BD:
        {
            for (i = 0; i < MP_ARRAY_SIZE(ps3remote_mapping); i++)
            {
                if (ps3remote_mapping[i].linux_keycode == ev->code)
                {
                    return ps3remote_mapping[i].mp_keycode;
                }
            }
            break;
        }
        case REMOTE_SATECHI_R2:
        {
            for (i = 0; i < MP_ARRAY_SIZE(r2remote_mapping); i++)
            {
                if (r2remote_mapping[i].linux_keycode == ev->code)
                {
                    return r2remote_mapping[i].mp_keycode;
                }
            }
            break;
        }
    }

    return INVALID_KEY;
}

static int input_remote_read(struct input_event *ev)
{
    // check for key press only
    if (ev->type != EV_KEY)
        return INVALID_KEY;

    // EvDev Key values:
    //  0: key release
    //  1: key press
    //
    if (ev->value == 0)
        return INVALID_KEY;

    //printf("%d\n\n", ev->code);

    return lookup_button_mp_key (ev);
}

#define EVDEV_MAX_EVENTS 32

#define USB_VENDOR_PS3REMOTE          0x054C
#define USB_DEVICE_PS3REMOTE          0x0306
#define USB_VENDOR_R2REMOTE           0x1915
#define USB_DEVICE_R2REMOTE           0xEEEE

static int scan_remote_controller(struct mp_input_src *src)
{
    int i, fd;

    // look for a valid remote controller device on system
    for (i = 0; i < EVDEV_MAX_EVENTS; i++)
    {
        struct input_id id;
        char file[64];
        char device_name[100];

        sprintf (file, "/dev/input/event%d", i);
        fd = open (file, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        if (ioctl(fd, EVIOCGID, &id) != -1 &&
            id.bustype == BUS_BLUETOOTH)
        {
            if (id.vendor == USB_VENDOR_PS3REMOTE && id.product == USB_DEVICE_PS3REMOTE)
            {
                MP_INFO (src, "Detected remote PS3 BD controller on %s\n", file);
                remote = REMOTE_PS3_BD;
                return fd;
            }
            if (id.vendor == USB_VENDOR_R2REMOTE && id.product == USB_DEVICE_R2REMOTE)
            {
                if (ioctl(fd, EVIOCGNAME(sizeof(device_name) - 1), &device_name) > 0)
                {
                    if (strncmp(device_name, "R2 Remote Keyboard", sizeof(device_name)) == 0)
                    {
                        MP_INFO (src, "Detected remote Satechi R2 Remote controller on %s\n", file);
                        remote = REMOTE_SATECHI_R2;
                        return fd;
                    }
                }
            }
            remote = REMOTE_UNKNOWN;
            close (fd);
        }
    }

    return -1;
}

static void request_cancel(struct mp_input_src *src)
{
    MP_VERBOSE(src, "exiting...\n");
    thread_exit_request = 1;
}

static void uninit(struct mp_input_src *src)
{
    MP_VERBOSE(src, "exited.\n");
    thread_exit_request = 0;
}

static void read_remote_controller_thread(struct mp_input_src *src, void *param)
{
    int inotify_fd = -1, inotify_wd = -1;
    int input_fd = -1, read_input, key;
    char buf[1000];
    fd_set set;
    struct timeval timeout;
    struct input_event event;

    input_fd = scan_remote_controller(src);

    inotify_fd = inotify_init();
    if (inotify_fd < 0)
    {
        MP_ERR(src, "Couldn't initialize inotify");
        goto exit;
    }

    inotify_wd = inotify_add_watch(inotify_fd, "/dev/input", IN_CREATE | IN_DELETE);
    if (inotify_wd < 0)
    {
        MP_ERR(src, "Couldn't add watch to /dev/input ");
        goto exit;
    }

    src->cancel = request_cancel;
    src->uninit = uninit;

    mp_input_src_init_done(src);

    while (thread_exit_request == 0)
    {
        FD_ZERO(&set);
        FD_SET(inotify_fd, &set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        if (select(inotify_fd + 1, &set, NULL, NULL, &timeout) > 0 &&
            FD_ISSET(inotify_fd, &set))
        {
            read(inotify_fd, buf, 1000);
            if (input_fd != -1)
                close(input_fd);
            input_fd = scan_remote_controller(src);
        }

        while (input_fd != -1)
        {
            read_input = read(input_fd, &event, sizeof (struct input_event));
            if (read_input < 0 && errno != EAGAIN)
            {
                close(input_fd);
                input_fd = -1;
                remote = REMOTE_UNKNOWN;
                break;
            }
            if (read_input < (int)sizeof (struct input_event))
            {
                break;
            }
            key = input_remote_read (&event);
            if (key != INVALID_KEY)
            {
                mp_input_put_key(src->input_ctx, key);
            }
        }

        usleep(10000);
    }

exit:

    if (inotify_wd != -1)
        inotify_rm_watch(inotify_fd, inotify_wd);
    if (inotify_fd != -1)
        close(inotify_fd);
    if (input_fd != -1)
        close(input_fd);
}

void mp_input_remote_controller_add(struct input_ctx *ictx)
{
    mp_input_add_thread_src(ictx, NULL, read_remote_controller_thread);
}

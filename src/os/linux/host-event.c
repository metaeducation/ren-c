/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 Atronix Engineering
**  Copyright 2012-2017 Rebol Open Source Contributors
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Title: Device: Event handler for X window
**  Purpose:
**      Processes X events to pass to REBOL
*/

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include  <X11/Xlib.h>

#include "reb-host.h"

#include "host-window.h"
#include "host-compositor.h"
#include "keysym2ucs.h"

enum {
    BUTTON_LEFT = 1,
    BUTTON_MIDDLE = 2,
    BUTTON_RIGHT = 3,
    BUTTON_SCROLL_UP = 4,
    BUTTON_SCROLL_DOWN = 5,
    BUTTON_SCROLL_LEFT = 6,
    BUTTON_SCROLL_RIGHT = 7
};

extern x_info_t *global_x_info;
REBGOB *Find_Gob_By_Window(Window win);
host_window_t *Find_Host_Window_By_ID(Window win);
void* Find_Compositor(REBGOB *gob);
REBEVT *RL_Find_Event (REBINT model, REBINT type);

typedef struct rebcmp_ctx REBCMP_CTX;
void rebcmp_blit_region(REBCMP_CTX* ctx, Region reg);
//void rebcmp_compose_region(REBCMP_CTX* ctx, REBGOB* winGob, REBGOB* gob, XRectangle *rect, REBOOL only);
#define GOB_HWIN(gob)   ((host_window_t*)Find_Window(gob))

#define GOB_COMPOSITOR(gob) (Find_Compositor(gob)) //gets handle to window's compositor
#define DOUBLE_CLICK_DIFF 300 /* in milliseconds */

#define MAX_WINDOWS 64
static REBGOB *resize_events[MAX_WINDOWS];

// Virtual key conversion table, sorted by first column.
static const REBCNT keysym_to_event[] = {
    /* 0xff09 */    XK_Tab,         EVK_NONE,   //EVK_NONE means it is passed 'as-is'
    /* 0xff50 */    XK_Home,        EVK_HOME,
    /* 0xff51 */    XK_Left,        EVK_LEFT,
    /* 0xff52 */    XK_Up,          EVK_UP,
    /* 0xff53 */    XK_Right,       EVK_RIGHT,
    /* 0xff54 */    XK_Down,        EVK_DOWN,
    /* 0xff55 */    XK_Page_Up,     EVK_PAGE_UP,
    /* 0xff56 */    XK_Page_Down,   EVK_PAGE_DOWN,
    /* 0xff57 */    XK_End,         EVK_END,
    /* 0xff63 */    XK_Insert,      EVK_INSERT,

    /* 0xff91 */    XK_KP_F1,       EVK_F1,
    /* 0xff92 */    XK_KP_F2,       EVK_F2,
    /* 0xff93 */    XK_KP_F3,       EVK_F3,
    /* 0xff94 */    XK_KP_F4,       EVK_F4,
    /* 0xff95 */    XK_KP_Home,     EVK_HOME,
    /* 0xff96 */    XK_KP_Left,     EVK_LEFT,
    /* 0xff97 */    XK_KP_Up,       EVK_UP,
    /* 0xff98 */    XK_KP_Right,    EVK_RIGHT,
    /* 0xff99 */    XK_KP_Down,     EVK_DOWN,
    /* 0xff9a */    XK_KP_Page_Up,  EVK_PAGE_UP,
    /* 0xff9b */    XK_KP_Page_Down, EVK_PAGE_DOWN,
    /* 0xff9c */    XK_KP_End,      EVK_END,
    /* 0xff9e */    XK_KP_Insert,   EVK_INSERT,
    /* 0xff9f */    XK_KP_Delete,   EVK_DELETE,

    /* 0xffbe */    XK_F1,          EVK_F1,
    /* 0xffbf */    XK_F2,          EVK_F2,
    /* 0xffc0 */    XK_F3,          EVK_F3,
    /* 0xffc1 */    XK_F4,          EVK_F4,
    /* 0xffc2 */    XK_F5,          EVK_F5,
    /* 0xffc3 */    XK_F6,          EVK_F6,
    /* 0xffc4 */    XK_F7,          EVK_F7,
    /* 0xffc5 */    XK_F8,          EVK_F8,
    /* 0xffc6 */    XK_F9,          EVK_F9,
    /* 0xffc7 */    XK_F10,         EVK_F10,
    /* 0xffc8 */    XK_F11,         EVK_F11,
    /* 0xffc9 */    XK_F12,         EVK_F12,
    /* 0xffff */    XK_Delete,      EVK_DELETE,
                    0x0,            0

};

static const REBCNT keysym_to_event_fallback[] = {
    /* 0xfe20 */    XK_ISO_Left_Tab,        0x09,   //Tab
                    0x0,            0
};

static void Add_Event_XY(REBGOB *gob, REBINT id, REBINT xy, REBINT flags)
{
    REBEVT evt;

    memset(&evt, 0, sizeof(evt));
    evt.type  = id;
    evt.flags = cast(u8, flags | EVF_HAS_XY);
    evt.model = EVM_GUI;
    evt.data  = xy;
    evt.eventee.ser = gob;

    rebEvent(&evt); // returns 0 if queue is full
}

static void Update_Event_XY(REBGOB *gob, REBINT id, REBINT xy, REBINT flags)
{
    REBEVT evt;

    memset(&evt, 0, sizeof(evt));
    evt.type  = id;
    evt.flags = cast(u8, flags | EVF_HAS_XY);
    evt.model = EVM_GUI;
    evt.data  = xy;
    evt.eventee.ser = gob;

    rebUpdateEvent(&evt);
}

static void Add_Event_Key(REBGOB *gob, REBINT id, REBINT key, REBINT flags)
{
    REBEVT evt;

    memset(&evt, 0, sizeof(evt));
    evt.type  = id;
    evt.flags = flags;
    evt.model = EVM_GUI;
    evt.data  = key;
    evt.eventee.ser = gob;

    rebEvent(&evt); // returns 0 if queue is full
}

static REBINT Check_Modifiers(REBINT flags, unsigned state)
{
    if (state & ShiftMask)
        flags |= EVF_SHIFT;
    if (state & ControlMask)
        flags |= EVF_CONTROL;
    return flags;
}

void X_Init_Resizing()
{
    resize_events[0] = NULL; /* reset resize_events, only resetting first one is enough */
}

void X_Finish_Resizing()
{
    /* send out resize */
    int i = 0;
    for (i = 0; i < MAX_WINDOWS; i ++) {
        if (resize_events[i] != NULL) {
            Resize_Window(resize_events[i], TRUE);
        } else {
            break; /* end of the array */
        }
    }
    X_Init_Resizing(); /* get ready for next call */
}

static void handle_property_notify(XEvent *ev, REBGOB *gob)
{
    /*
       REBYTE *target = XGetAtomName(global_x_info->display, ev->xproperty.atom);
       printf("Property (%s, %d) changed: %d\n", target, ev->xproperty.atom, ev->xproperty.state);
       XFree(target);
       */
    Atom XA_WM_STATE = x_atom_list_find_atom(global_x_info->x_atom_list,
                                             global_x_info->display,
                                             "_NET_WM_STATE",
                                             False);
    Atom XA_FULLSCREEN = x_atom_list_find_atom(global_x_info->x_atom_list,
                                               global_x_info->display,
                                               "_NET_WM_STATE_FULLSCREEN",
                                               False);
    Atom XA_MAX_HORZ = x_atom_list_find_atom(global_x_info->x_atom_list,
                                             global_x_info->display,
                                             "_NET_WM_STATE_MAXIMIZED_HORZ",
                                             False);
    Atom XA_MAX_VERT = x_atom_list_find_atom(global_x_info->x_atom_list,
                                             global_x_info->display,
                                             "_NET_WM_STATE_MAXIMIZED_VERT",
                                             False);
    Atom XA_ABOVE = x_atom_list_find_atom(global_x_info->x_atom_list,
                                          global_x_info->display,
                                          "_NET_WM_STATE_ABOVE",
                                          False);
    Atom XA_HIDDEN = x_atom_list_find_atom(global_x_info->x_atom_list,
                                           global_x_info->display,
                                           "_NET_WM_STATE_HIDDEN",
                                           False);
    if (!XA_WM_STATE
        || !XA_FULLSCREEN
        || !XA_MAX_HORZ
        || !XA_MAX_VERT
        || gob == NULL){
        return;
    }

    //printf("XA_WM_STATE: %d\n", XA_WM_STATE);

    if (ev->xproperty.atom == XA_WM_STATE) {
        Atom     actual_type;
        int      actual_format;
        long     nitems;
        long     bytes;
        Atom     *data = NULL;
        int i = 0;
        int maximized_horz = 0;
        int maximized_vert = 0;
        int fullscreen = 0;
        int on_top = 0;
        int hidden = 0;
        int old_maximized = GET_GOB_FLAG(gob, GOBF_MAXIMIZE);
        int old_fullscreen = GET_GOB_FLAG(gob, GOBF_FULLSCREEN);
        int old_hidden = GET_GOB_FLAG(gob, GOBF_HIDDEN);
        host_window_t *hw = GOB_HWIN(gob);
        XGetWindowProperty(global_x_info->display,
                           ev->xproperty.window,
                           XA_WM_STATE,
                           0,
                           (~0L),
                           False,
                           XA_ATOM,
                           &actual_type,
                           &actual_format,
                           &nitems,
                           &bytes,
                           (unsigned char**)&data);
        for(i = 0; i < nitems; i ++){
            if (data[i] == XA_FULLSCREEN){
                //printf("Window %x is Fullscreen\n", ev->xproperty.window);
                fullscreen = 1;
            } else if (data[i] == XA_MAX_HORZ) {
                maximized_horz = 1;
            } else if (data[i] == XA_MAX_VERT) {
                maximized_vert = 1;
            } else if (data[i] == XA_ABOVE) {
                on_top = 1;
            } else if (data[i] == XA_HIDDEN) {
                hidden = 1;
            }
        }

        if (data != NULL){
            XFree(data);
        }

        if (fullscreen) {
            CLR_GOB_FLAG(gob, GOBF_MAXIMIZE);
            SET_GOB_FLAG(gob, GOBF_FULLSCREEN);
        } else {
            //printf("Not fullscreen\n");
            CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
        }

        if (maximized_horz && maximized_vert) {
            CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
            SET_GOB_FLAG(gob, GOBF_MAXIMIZE);
        } else {
            //printf("Not maxed\n");
            CLR_GOB_FLAG(gob, GOBF_MAXIMIZE);
        }

        if (on_top) {
            SET_GOB_FLAG(gob, GOBF_TOP);
        } else {
            //printf("Not no_top\n");
            CLR_GOB_FLAG(gob, GOBF_TOP);
        }

        if (hidden) {
            SET_GOB_FLAG(gob, GOBF_HIDDEN);
        } else {
            //printf("Not hidden\n");
            CLR_GOB_FLAG(gob, GOBF_HIDDEN);
        }
        hw->window_flags = gob->flags; /* save a copy of current window flags */
    } else {
        //printf("Not WM_STATE, ignoring\n");
    }
}

static void handle_button(XEvent *ev, REBGOB *gob)
{
    //printf("Button %d event at %d\n", ev->xbutton.button, ev->xbutton.time);
    static Time last_click = 0;
    static REBINT last_click_button = 0;
    // Handle XEvents and flush the input
    REBINT xyd = 0;
    REBEVT *evt = NULL;
    xyd = (ROUND_TO_INT(PHYS_COORD_X(ev->xbutton.x))) + (ROUND_TO_INT(PHYS_COORD_Y(ev->xbutton.y)) << 16);
    REBINT id = 0, flags = 0;
    flags = Check_Modifiers(0, ev->xbutton.state);
    if (ev->xbutton.button < 4) {
        if (ev->type == ButtonPress
            && last_click_button == ev->xbutton.button
            && ev->xbutton.time - last_click < DOUBLE_CLICK_DIFF){
            /* FIXME, a hack to detect double click: a double click would be a single click followed by a double click */
            flags |= 1 << EVF_DOUBLE;
            //printf("Button %d double clicked\n", ev->xbutton.button);
        }
        switch (ev->xbutton.button){
            case BUTTON_LEFT:
                id = (ev->type == ButtonPress)? EVT_DOWN: EVT_UP;
                break;
            case BUTTON_MIDDLE:
                id = (ev->type == ButtonPress)? EVT_AUX_DOWN: EVT_AUX_UP;
                break;
            case BUTTON_RIGHT:
                id = (ev->type == ButtonPress)? EVT_ALT_DOWN: EVT_ALT_UP;
                break;
        }
        Add_Event_XY(gob, id, xyd, flags);
    } else {
        if (ev->type == ButtonRelease) {
            evt = RL_Find_Event(EVM_GUI,
                                ev->xbutton.state & ControlMask? EVT_SCROLL_PAGE: EVT_SCROLL_LINE);
            u32 data = 0;
            u32 *pdata = NULL;
            i16 tmp = 0;
            if (evt != NULL) {
                pdata = &evt->data;
            } else {
                pdata = &data;
            }
            int mw_num_lines = 3;

            if (ev->xbutton.button == BUTTON_SCROLL_UP
                || ev->xbutton.button == BUTTON_SCROLL_DOWN) {
                tmp = *pdata >> 16;
            } else if (ev->xbutton.button == BUTTON_SCROLL_LEFT
                       || ev->xbutton.button == BUTTON_SCROLL_RIGHT) {
                tmp = *pdata & 0xFFFF;
            } else {
                return;
            }

            if (ev->xbutton.button == BUTTON_SCROLL_UP
                || ev->xbutton.button == BUTTON_SCROLL_RIGHT) {
                if (tmp < 0){
                    tmp = 0;
                }
                if (tmp <= 0x7FFF - mw_num_lines) { /* avoid overflow */
                    tmp += mw_num_lines;
                }
            } else if (ev->xbutton.button == BUTTON_SCROLL_DOWN
                       || ev->xbutton.button == BUTTON_SCROLL_LEFT) {
                tmp = *pdata & 0xFFFF;
                if (tmp > 0){
                    tmp = 0;
                }
                if (tmp > -0x8000 + mw_num_lines) { /* avoid overflow */
                    tmp -= mw_num_lines;
                }
            }

            if (ev->xbutton.button == BUTTON_SCROLL_UP
                || ev->xbutton.button == BUTTON_SCROLL_DOWN) {
                *pdata = (tmp << 16) | (*pdata & 0xFFFF); /* do not touch low 16-bit */
            } else if (ev->xbutton.button == BUTTON_SCROLL_LEFT
                       || ev->xbutton.button == BUTTON_SCROLL_RIGHT) {
                *pdata = (tmp & 0xFFFF) | (*pdata & 0xFFFF0000); /* do not touch high 16-bit */
            }

            if (evt == NULL) {
                Add_Event_XY(gob,
                             ev->xbutton.state & ControlMask? EVT_SCROLL_PAGE: EVT_SCROLL_LINE,
                             data, 0);
            }
        }
    }
    if (ev->type == ButtonPress) {
        last_click_button = ev->xbutton.button;
        last_click = ev->xbutton.time;
    }
}

static void handle_client_message(XEvent *ev)
{
    /*
       const REBYTE *message_type = XGetAtomName(global_x_info->display, ev->xclient.message_type);
       const REBYTE *protocol = XGetAtomName(global_x_info->display, ev->xclient.data.l[0]);
       printf("client message: %s, %s\n", message_type, protocol);
       XFree(message_type);
       XFree(protocol);
       */
    Atom XA_DELETE_WINDOW = x_atom_list_find_atom(global_x_info->x_atom_list,
                                                  global_x_info->display,
                                                  "WM_DELETE_WINDOW",
                                                  False);
    Atom XA_PING = x_atom_list_find_atom(global_x_info->x_atom_list,
                                         global_x_info->display,
                                         "_NET_WM_PING",
                                         False);
    REBGOB *gob = NULL;
    if (XA_DELETE_WINDOW
        && XA_DELETE_WINDOW == ev->xclient.data.l[0]) {
        gob = Find_Gob_By_Window(ev->xclient.window);
        if (gob != NULL){
            Add_Event_XY(gob, EVT_CLOSE, 0, 0);
        }
    } else if (XA_PING
               && XA_PING == ev->xclient.data.l[0]) {
        //printf("Ping from window manager\n");
        ev->xclient.window = DefaultRootWindow(global_x_info->display);
        XSendEvent(global_x_info->display,
                   ev->xclient.window,
                   False,
                   (SubstructureNotifyMask | SubstructureRedirectMask),
                   ev);
    }
}

static void handle_selection_request(XEvent *ev)
{
    XEvent selection_event;
#if 0
    const REBYTE *target = XGetAtomName(global_x_info->display, ev->xselectionrequest.target);
    const REBYTE *property = XGetAtomName(global_x_info->display, ev->xselectionrequest.property);
    printf("selection target = %s, property = %s\n", target, property);
    XFree((void*)property);
    XFree((void*)target);
#endif
    Atom XA_UTF8_STRING = x_atom_list_find_atom(global_x_info->x_atom_list,
                                                global_x_info->display,
                                                "UTF8_STRING",
                                                True);
    Atom XA_TARGETS = x_atom_list_find_atom(global_x_info->x_atom_list,
                                            global_x_info->display,
                                            "TARGETS",
                                            True);
    Atom XA_CLIPBOARD = x_atom_list_find_atom(global_x_info->x_atom_list,
                                              global_x_info->display,
                                              "CLIPBOARD",
                                              True);
    selection_event.type = SelectionNotify;
    if (ev->xselectionrequest.target == XA_TARGETS) {
        selection_event.xselection.property = ev->xselectionrequest.property;
        Atom targets[] = {XA_TARGETS, XA_UTF8_STRING, XA_STRING};
        XChangeProperty(global_x_info->display,
                        ev->xselectionrequest.requestor,
                        ev->xselectionrequest.property,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        (unsigned char*)&targets,
                        sizeof(targets)/sizeof(targets[0]));
    } else if (ev->xselectionrequest.target == XA_STRING
               || ev->xselectionrequest.target == XA_UTF8_STRING) {
        selection_event.xselection.property = ev->xselectionrequest.property;
        XChangeProperty(global_x_info->display,
                        ev->xselectionrequest.requestor,
                        ev->xselectionrequest.property,
                        ev->xselectionrequest.target,
                        8,          /* format, unsigned short */
                        PropModeReplace,
                        global_x_info->selection.data,
                        global_x_info->selection.data_length);
    } else {
        selection_event.xselection.property = 0;
    }
    selection_event.xselection.send_event = 1;
    selection_event.xselection.display = ev->xselectionrequest.display;
    selection_event.xselection.requestor = ev->xselectionrequest.requestor;
    selection_event.xselection.selection = ev->xselectionrequest.selection;
    selection_event.xselection.target = ev->xselectionrequest.target;
    selection_event.xselection.time = ev->xselectionrequest.time;
    //printf("Sending selection_event\n");
    XSendEvent(selection_event.xselection.display,
               selection_event.xselection.requestor,
               False,
               0,
               &selection_event);
}

static void handle_selection_notify(XEvent *ev)
{
    Atom XA_UTF8_STRING = x_atom_list_find_atom(global_x_info->x_atom_list,
                                                global_x_info->display,
                                                "UTF8_STRING",
                                                True);
    Atom XA_TARGETS = x_atom_list_find_atom(global_x_info->x_atom_list,
                                            global_x_info->display,
                                            "TARGETS",
                                            True);
    Atom XA_CLIPBOARD = x_atom_list_find_atom(global_x_info->x_atom_list,
                                              global_x_info->display,
                                              "CLIPBOARD",
                                              True);
    if (ev->xselection.target == XA_TARGETS){
        Atom     actual_type;
        int      actual_format;
        long     nitems;
        long     bytes;
        Atom     *data = NULL;
        int      status;
        if (ev->xselection.property){
            status = XGetWindowProperty(ev->xselection.display,
                                        ev->xselection.requestor,
                                        ev->xselection.property,
                                        0,
                                        (~0L),
                                        False,
                                        XA_ATOM,
                                        &actual_type,
                                        &actual_format,
                                        &nitems,
                                        &bytes,
                                        (unsigned char**)&data);
            int i = 0;
            for(i = 0; i < nitems; i ++){
                if (data[i] == XA_UTF8_STRING
                    || data[i] == XA_STRING) {
                    XConvertSelection(ev->xselection.display,
                                      XA_CLIPBOARD,
                                      data[i],
                                      ev->xselection.property,
                                      ev->xselection.requestor,
                                      CurrentTime);
                    break;
                }
            }
        }
    } else if (ev->xselection.target == XA_UTF8_STRING
               || ev->xselection.target == XA_STRING) {
        global_x_info->selection.property = ev->xselection.property;
        global_x_info->selection.status = 1; /* response received */
    }
}

static void handle_configure_notify(XEvent *ev, REBGOB *gob)
{
    XConfigureEvent xce = ev->xconfigure;
    REBINT xyd = 0;
    /* translate x,y to its gob_parent coordinates */
    int x = xce.x, y = xce.y;
    /*
       printf("configuranotify, window = %x, x = %d, y = %d, w = %d, h = %d\n",
       xce.window,
       xce.x, xce.y, xce.width, xce.height);
       */
    REBGOB *gob_parent = GOB_TMP_OWNER(gob);
    if (gob_parent != NULL) {
        host_window_t *hw = GOB_HWIN(gob_parent);
        if (hw != NULL) {
            Window gob_parent_window = hw->x_id;
            Window child;
            if (GET_GOB_FLAG(gob, GOBF_POPUP)) {
                /* for popup windows, the x, y are in screen coordinates, see OS_Create_Window */
                if (hw->x_parent_id != DefaultRootWindow(xce.display)) {
                    XTranslateCoordinates(xce.display,
                            xce.window,
                            DefaultRootWindow(xce.display),
                            0, 0,
                            &x, &y, &child);
                }
            } else {
                XTranslateCoordinates(xce.display,
                        xce.window,
                        hw->x_parent_id,
                        0, 0,
                        &x, &y, &child);
            }
            //printf("XTranslateCoordinates returns %d, pos: %dx%d\n", status, x, y);
        }
    }
    if (ROUND_TO_INT(gob->offset.x) != x
        || ROUND_TO_INT(gob->offset.y) != y){
        /*
           printf("%s, %s, %d: EVT_OFFSET (%dx%d) is sent\n", __FILE__, __func__, __LINE__,
           ROUND_TO_INT(x), ROUND_TO_INT(y));
           */
        gob->offset.x = ROUND_TO_INT(PHYS_COORD_X(x));
        gob->offset.y = ROUND_TO_INT(PHYS_COORD_X(y));
        xyd = (ROUND_TO_INT(gob->offset.x)) + (ROUND_TO_INT(gob->offset.y) << 16);
        Update_Event_XY(gob, EVT_OFFSET, xyd, 0);
        /* avoid a XMoveWindow call from OS_Update_Window */
        GOB_XO(gob) = GOB_LOG_X(gob);
        GOB_YO(gob) = GOB_LOG_Y(gob);
    }
    host_window_t* hw = Find_Host_Window_By_ID(ev->xconfigure.window);
    assert(hw != NULL);
    if (hw->old_width == xce.width && hw->old_height == xce.height) {
        /* XResizeWindow failed, or this is a window movement */
        return;
    }
    gob->size.x = ROUND_TO_INT(PHYS_COORD_X(hw->old_width = xce.width));
    gob->size.y = ROUND_TO_INT(PHYS_COORD_Y(hw->old_height = xce.height));
    xyd = (ROUND_TO_INT((gob->size.x))) + (ROUND_TO_INT(gob->size.y) << 16);
    if (GOB_WO_INT(gob) != GOB_LOG_W_INT(gob)
        || GOB_HO_INT(gob) != GOB_LOG_H_INT(gob)) {
        //printf("Resize for gob: %x to %dx%d\n", gob, GOB_LOG_W_INT(gob), GOB_LOG_H_INT(gob));
        //printf("%s, %s, %d: EVT_RESIZE is sent: %x\n", __FILE__, __func__, __LINE__, xyd);
        int i = 0;
        for(i = 0; i < MAX_WINDOWS; i ++){
            if (resize_events[i] == NULL){
                //printf("Filled resize_events[%d]\n", i);
                resize_events[i] = gob;
                if (i < MAX_WINDOWS - 1) {
                    resize_events[i + 1] = NULL; /* mark it the end of the array */
                }
                break;
            }
            if (resize_events[i] == gob)
                break;
        }
        Update_Event_XY(gob, EVT_RESIZE, xyd, 0);
    }
}

static void handle_key(XEvent *ev, REBGOB *gob)
{
    KeySym keysym;
    REBINT flags = Check_Modifiers(0, ev->xkey.state);
    char key_string[8];
    XComposeStatus compose_status;
    int i = 0, key = -1;
    int len = XLookupString(&ev->xkey, key_string, sizeof(key_string), &keysym, &compose_status);
    key_string[len] = '\0';
    //RL_Print ("key %s (%x) is released\n", key_string, key_string[0]);

    for (i = 0; keysym_to_event[i] && keysym > keysym_to_event[i]; i += 2);
    if (keysym == keysym_to_event[i]) {
        if (keysym_to_event[i + 1] == EVK_NONE) {
            key = key_string[0]; /* pass-thru */
        } else {
            key = keysym_to_event[i + 1] << 16;
        }
    } else {
        key = keysym2ucs(keysym);
        if (key < 0 && len > 0){
            key = key_string[0]; /* FIXME, key_string could be longer than 1 */
        }
        /* map control characters */
        if (LOGICAL(flags & EVF_CONTROL) && NOT(flags & EVF_SHIFT)) {
            if (key >= 'A' && key <= '_') {
                key = key - 'A' + 1;
            } else if (key >= 'a' && key <= 'z') {
                key = key - 'a' + 1;
            }
        }
    }

    if (key > 0){
        Add_Event_Key(gob,
                      ev->type == KeyPress? EVT_KEY : EVT_KEY_UP,
                      key, flags);

        /*
           RL_Print ("Key event %s with key %x (flags: %x) is sent\n",
           ev->type == KeyPress? "EVT_KEY" : "EVT_KEY_UP",
           key,
           flags);
           */
    } else {
        for (i = 0; keysym_to_event_fallback[i] && keysym > keysym_to_event_fallback[i]; i += 2);
        if (keysym == keysym_to_event_fallback[i] && keysym_to_event_fallback[i + 1] > 0) {
            Add_Event_Key(gob,
                          ev->type == KeyPress? EVT_KEY : EVT_KEY_UP,
                          keysym_to_event_fallback[i + 1], flags);

        }
    }
}

static void handle_expose(XEvent *ev, REBGOB *gob)
{
    host_window_t *hw = GOB_HWIN(gob);

    XRectangle rect = {ev->xexpose.x, ev->xexpose.y, ev->xexpose.width, ev->xexpose.height}; /* in screen coordinates */

    assert (hw != NULL);
    if (hw == NULL) {
        return;
    }

    if (hw->exposed_region == NULL) {
        hw->exposed_region = XCreateRegion();
    }
    XUnionRectWithRegion(&rect, hw->exposed_region, hw->exposed_region);
    if (ev->xexpose.count == 0){
        /* find wingob, copied from Draw_Window */
        REBGOB *wingob = gob;
        while (GOB_PARENT(wingob) && GOB_PARENT(wingob) != Gob_Root
               && GOB_PARENT(wingob) != wingob) // avoid infinite loop
            wingob = GOB_PARENT(wingob);

        //check if it is really open
        if (!IS_WINDOW(wingob) || !GET_GOB_STATE(wingob, GOBS_OPEN)) return;

        void *compositor = GOB_COMPOSITOR(gob);
        assert (compositor != NULL);

        /*
        XRectangle final_rect;
        XClipBox(hw->exposed_region, &final_rect);
        printf("Win Region , left: %d,\ttop: %d,\tright: %d,\tbottom: %d\n",
                 rect.x,
                 rect.y,
                 rect.x + rect.width,
                 rect.y + rect.height);
        printf("exposed: x %d, y %d, w %d, h %d\n", final_rect.x, final_rect.y, final_rect.width, final_rect.height);
        */
        //rebcmp_compose_region(compositor, wingob, gob, &final_rect, FALSE);
        rebcmp_blit_region(compositor, hw->exposed_region);

        XDestroyRegion(hw->exposed_region);
        hw->exposed_region = NULL;
    }
}

void Dispatch_Event(XEvent *ev)
{
    REBGOB *gob = NULL;
    // Handle XEvents and flush the input
    REBINT flags = 0;
    if (resize_events[0] != NULL
        && ev->type != ConfigureNotify) {/* handle expose after resizing */
        if (ev->type == Expose) { /* ignore expose after resize */
            int i = 0;
            gob = Find_Gob_By_Window(ev->xexpose.window);
            for (i = 0; i < MAX_WINDOWS; i ++) {
                if (resize_events[i] == NULL) {
                    break;
                } else if (resize_events[i] == gob) {
                    return;
                }
            }
        }
        X_Finish_Resizing();
    }
    switch (ev->type) {
        case CreateNotify:
            /*
            printf("window %x created at: %dx%d, size: %dx%d\n",
                     ev->xcreatewindow.window,
                     ev->xcreatewindow.x, ev->xcreatewindow.y,
                     ev->xcreatewindow.width, ev->xcreatewindow.height);
                     */
            break;
        case Expose:
            //printf("exposed\n");
            gob = Find_Gob_By_Window(ev->xexpose.window);
            if (gob != NULL) {
                handle_expose(ev, gob);
            }
            break;

        case ButtonPress:
        case ButtonRelease:
            gob = Find_Gob_By_Window(ev->xbutton.window);
            if (gob != NULL)
                handle_button(ev, gob);
            break;

        case MotionNotify:
            //printf("mouse motion\n");
            gob = Find_Gob_By_Window(ev->xmotion.window);
            if (gob != NULL){
                REBINT xyd = (ROUND_TO_INT(PHYS_COORD_X(ev->xmotion.x))) + (ROUND_TO_INT(PHYS_COORD_Y(ev->xmotion.y)) << 16);
                Update_Event_XY(gob, EVT_MOVE, xyd, 0);
            }
            break;
        case KeyPress:
        case KeyRelease:
            gob = Find_Gob_By_Window(ev->xkey.window);
            if(gob != NULL)
                handle_key(ev, gob);

            break;
        case ResizeRequest:
            //RL_Print ("request to resize to %dx%d", ev->xresizerequest.width, ev->xresizerequest.height);
            break;
        case FocusIn:
            if (ev->xfocus.mode != NotifyWhileGrabbed) {
                //RL_Print ("FocusIn, type = %d, window = %x\n", ev->xfocus.type, ev->xfocus.window);
                gob = Find_Gob_By_Window(ev->xfocus.window);
                if (gob && !GET_GOB_STATE(gob, GOBS_ACTIVE)) {
                    SET_GOB_STATE(gob, GOBS_ACTIVE);
                    Add_Event_XY(gob, EVT_ACTIVE, 0, 0);
                }
            }
            break;
        case FocusOut:
            if (ev->xfocus.mode != NotifyWhileGrabbed) {
                //RL_Print ("FocusOut, type = %d, window = %x\n", ev->xfocus.type, ev->xfocus.window);
                gob = Find_Gob_By_Window(ev->xfocus.window);
                if (gob && GET_GOB_STATE(gob, GOBS_ACTIVE)) {
                    CLR_GOB_STATE(gob, GOBS_ACTIVE);
                    Add_Event_XY(gob, EVT_INACTIVE, 0, 0);
                }
            }
            break;
        case DestroyNotify:
            //RL_Print ("destroyed %x\n", ev->xdestroywindow.window);
            gob = Find_Gob_By_Window(ev->xdestroywindow.window);
            if (gob != NULL){
                host_window_t *hw = GOB_HWIN(gob);
                if (hw != NULL) {
                    OS_FREE(hw);
                }
                CLR_GOB_STATE(gob, GOBS_OPEN);
                CLR_GOB_STATE(gob, GOBS_ACTIVE);
                Free_Window(gob);
            }
            break;
        case ClientMessage:
            //printf("closed\n");
            handle_client_message(ev);
            break;
        case PropertyNotify:
            /* check if it's fullscreen */
            gob = Find_Gob_By_Window(ev->xproperty.window); /*this event could come after window is free'ed */
            if (gob != NULL)
                handle_property_notify(ev, gob);
            break;
        case ConfigureNotify:
            gob = Find_Gob_By_Window(ev->xconfigure.window);
            if (gob != NULL) {
                handle_configure_notify(ev, gob);
            }
            break;
        case SelectionRequest:
            //printf("SelectionRequest\n");
            handle_selection_request(ev);
            break;
        case SelectionNotify:
            //printf("SelectionNotify\n");
            handle_selection_notify(ev);
            break;
        case SelectionClear:
            if (global_x_info->selection.data != NULL) {
                OS_FREE(global_x_info->selection.data);
                global_x_info->selection.data = NULL;
                global_x_info->selection.data_length = 0;
            }
            break;
        case MapNotify:
            //printf("Window %x is mapped\n", ev->xmap.window);
            {
                host_window_t *hw = Find_Host_Window_By_ID(ev->xmap.window);
                if (hw != NULL) {
                    hw->mapped = 1;
                }
            }
            break;
        case ReparentNotify:
            //printf("Window %x is reparented to %x\n", ev->xreparent.window, ev->xreparent.parent);
            {
                host_window_t *hw = Find_Host_Window_By_ID(ev->xreparent.window);
                if (hw != NULL) {
                    hw->x_parent_id = ev->xreparent.parent;
                }
            }
            break;
        default:
            //printf("default event type: %d\n", ev->type);
            break;
    }
}

void X_Event_Loop(int at_most)
{
    XEvent ev;
    int n = 0;
    if (global_x_info->display == NULL) {
        return;
    }
    X_Init_Resizing();
    while(XPending(global_x_info->display) && (at_most < 0 || n < at_most)) {
        ++ n;
        XNextEvent(global_x_info->display, &ev);
        Dispatch_Event(&ev);
    }
    X_Finish_Resizing();
}


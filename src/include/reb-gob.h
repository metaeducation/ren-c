//
//  File: %reb-gob.h
//  Summary: "Graphical compositing objects"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// GOBs are lower-level graphics object used by the compositing
// and rendering system. Because a GUI can contain thousands of
// GOBs, they are designed and structured to be simple and small.
// Note that GOBs are also used for windowing.
//
// GOBs are allocated from a special pool and
// are accounted for by the standard garbage collector.
//

// We accept GOB for the moment in Core, but not view in general...
// Ultimatley GOB represents a category of Ren/C external items that
// can participate with the system and its GC but are not part of core.
//
// Atronix repository included host-view.h while rebol open source didn't

// #include "host-view.h"

enum GOB_FLAGS {        // GOB attribute and option flags
    GOBF_USED = 0,
    GOBF_MARK,
    GOBF_TOP,           // Top level (window or output image)
    GOBF_WINDOW,        // Window (parent is OS window reference)
    GOBF_OPAQUE,        // Has no alpha
    GOBF_STATIC,        // Does not change
    GOBF_HIDDEN,        // Is hidden (e.g. hidden window)
    GOBF_RESIZE,        // Can be resized
    GOBF_NO_TITLE,      // Has window title
    GOBF_NO_BORDER,     // Has window border
    GOBF_DROPABLE,      // Let window receive drag and drop
    GOBF_TRANSPARENT,   // Window is in transparent mode
    GOBF_POPUP,         // Window is a popup (with owner window)
    GOBF_MODAL,         // Modal event filtering
    GOBF_ON_TOP,        // The window is always on top
    GOBF_ACTIVE,        // Window is active
    GOBF_MINIMIZE,      // Window is minimized
    GOBF_MAXIMIZE,      // Window is maximized
    GOBF_RESTORE,       // Window is restored
    GOBF_FULLSCREEN,    // Window is fullscreen
    GOBF_MAX
};

enum GOB_STATE {        // GOB state flags
    GOBS_OPEN = 0,      // Window is open
    GOBS_ACTIVE,        // Window is active
    GOBS_NEW,           // Gob is new to pane (old-offset, old-size wrong)
    GOBS_MAX
};

enum GOB_TYPES {        // Types of content
    GOBT_NONE = 0,
    GOBT_COLOR,
    GOBT_IMAGE,
    GOBT_STRING,
    GOBT_DRAW,
    GOBT_TEXT,
    GOBT_EFFECT,
    GOBT_MAX
};

enum GOB_DTYPES {       // Userdata types
    GOBD_NONE = 0,
    GOBD_OBJECT,
    GOBD_BLOCK,
    GOBD_STRING,
    GOBD_BINARY,
    GOBD_RESV,          // unicode
    GOBD_INTEGER,
    GOBD_MAX
};

#pragma pack(4)

typedef struct rebol_gob REBGOB;

struct rebol_gob {
    struct Reb_Header header;

    REBCNT state;       // state flags

    REBSER *pane;       // List of child GOBs
    REBGOB *parent;     // Parent GOB (or window ptr)

    REBYTE alpha;       // transparency
    REBYTE ctype;       // content data type
    REBYTE dtype;       // pointer data type
    REBYTE resv;        // reserved

    REBGOB *owner;      // !!! was a singular item in a union

    REBSER *content;    // content value (block, string, color)
    REBSER *data;       // user defined data

    REBXYF offset;      // location
    REBXYF size;
    REBXYF old_offset;  // prior location
    REBXYF old_size;    // prior size

#if defined(__LP64__) || defined(__LLP64__)
    //
    // Depending on how the fields are arranged, this may require padding to
    // make sure the REBNOD-derived type is a multiple of 64-bits in size.
    //
#endif
};
#pragma pack()

typedef struct gob_window {             // Maps gob to window
    REBGOB *gob;
    void* win;
    void* compositor;
} REBGOBWINDOWS;

#define GOB_X(g)        ((g)->offset.x)
#define GOB_Y(g)        ((g)->offset.y)
#define GOB_W(g)        ((g)->size.x)
#define GOB_H(g)        ((g)->size.y)

#define GOB_LOG_X(g)        (LOG_COORD_X((g)->offset.x))
#define GOB_LOG_Y(g)        (LOG_COORD_Y((g)->offset.y))
#define GOB_LOG_W(g)        (LOG_COORD_X((g)->size.x))
#define GOB_LOG_H(g)        (LOG_COORD_Y((g)->size.y))

#define GOB_X_INT(g)    ROUND_TO_INT((g)->offset.x)
#define GOB_Y_INT(g)    ROUND_TO_INT((g)->offset.y)
#define GOB_W_INT(g)    ROUND_TO_INT((g)->size.x)
#define GOB_H_INT(g)    ROUND_TO_INT((g)->size.y)

#define GOB_LOG_X_INT(g)    ROUND_TO_INT(LOG_COORD_X((g)->offset.x))
#define GOB_LOG_Y_INT(g)    ROUND_TO_INT(LOG_COORD_Y((g)->offset.y))
#define GOB_LOG_W_INT(g)    ROUND_TO_INT(LOG_COORD_X((g)->size.x))
#define GOB_LOG_H_INT(g)    ROUND_TO_INT(LOG_COORD_Y((g)->size.y))

#define GOB_XO(g)       ((g)->old_offset.x)
#define GOB_YO(g)       ((g)->old_offset.y)
#define GOB_WO(g)       ((g)->old_size.x)
#define GOB_HO(g)       ((g)->old_size.y)
#define GOB_XO_INT(g)   ROUND_TO_INT((g)->old_offset.x)
#define GOB_YO_INT(g)   ROUND_TO_INT((g)->old_offset.y)
#define GOB_WO_INT(g)   ROUND_TO_INT((g)->old_size.x)
#define GOB_HO_INT(g)   ROUND_TO_INT((g)->old_size.y)

#define CLEAR_GOB_STATE(g) ((g)->state = 0)

#define SET_GOB_FLAG(g,f) \
    cast(void, (g)->header.bits |= (cast(REBUPT, 1) << (f)))

#define GET_GOB_FLAG(g,f) \
    LOGICAL((g)->header.bits & (cast(REBUPT, 1) << (f)))

#define CLR_GOB_FLAG(g,f) \
    cast(void, (g)->header.bits &= ~(cast(REBUPT, 1) << (f)))


#define SET_GOB_STATE(g,f)      SET_FLAG((g)->state, f)
#define GET_GOB_STATE(g,f)      GET_FLAG((g)->state, f)
#define CLR_GOB_STATE(g,f)      CLR_FLAG((g)->state, f)

#define GOB_ALPHA(g)        ((g)->alpha)
#define GOB_TYPE(g)         ((g)->ctype)
#define SET_GOB_TYPE(g,t)   ((g)->ctype = (t))
#define GOB_DTYPE(g)        ((g)->dtype)
#define SET_GOB_DTYPE(g,t)  ((g)->dtype = (t))
#define GOB_DATA(g)         ((g)->data)
#define SET_GOB_DATA(g,v)   ((g)->data = (v))
#define GOB_TMP_OWNER(g)    ((g)->owner)

#define IS_GOB_OPAQUE(g)  GET_GOB_FLAG(g, GOBF_OPAQUE)
#define SET_GOB_OPAQUE(g) SET_GOB_FLAG(g, GOBF_OPAQUE)
#define CLR_GOB_OPAQUE(g) CLR_GOB_FLAG(g, GOBF_OPAQUE)

#define GOB_PANE(g)     ((g)->pane)
#define GOB_PARENT(g)   ((g)->parent)
#define GOB_CONTENT(g)  ((g)->content)

// Control dependencies on series structures:
#ifdef REB_DEF
#define GOB_STRING(g)       SER_HEAD(GOB_CONTENT(g))
#define GOB_LEN(g)          SER_LEN((g)->pane)
#define SET_GOB_LEN(g,l)    SET_SERIES_LEN((g)->pane, (l))
#define GOB_HEAD(g)         SER_HEAD(REBGOB*, GOB_PANE(g))
#else
#define GOB_STRING(g)   ((REBYTE *)RL_Series(GOB_CONTENT(g), (REBCNT)RXI_SER_DATA))
#define GOB_LEN(g)     ((REBCNT)RL_Series(GOB_PANE(g), (REBCNT)RXI_SER_TAIL))
#define GOB_HEAD(g)     ((REBGOB **)RL_Series(GOB_PANE(g), (REBCNT)RXI_SER_DATA))
#endif
#define GOB_BITMAP(g)   GOB_STRING(g)
#define GOB_AT(g,n)   (GOB_HEAD(g)+n)

#define IS_WINDOW(g)    (GOB_PARENT(g) == Gob_Root && GET_GOB_FLAG(g, GOBF_WINDOW))

#define IS_GOB_COLOR(g)  (GOB_TYPE(g) == GOBT_COLOR)
#define IS_GOB_DRAW(g)   (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_DRAW)
#define IS_GOB_IMAGE(g)  (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_IMAGE)
#define IS_GOB_EFFECT(g) (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_EFFECT)
#define IS_GOB_STRING(g) (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_STRING)
#define IS_GOB_TEXT(g)   (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_TEXT)

#define IS_GOB_MARK(g)  GET_GOB_FLAG((g), GOBF_MARK)
#define MARK_GOB(g)     SET_GOB_FLAG((g), GOBF_MARK)
#define UNMARK_GOB(g)   CLR_GOB_FLAG((g), GOBF_MARK)

extern REBGOB *Gob_Root; // Top level GOB (the screen)

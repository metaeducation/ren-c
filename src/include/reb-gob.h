//
//  File: %reb-gob.h
//  Summary: "Graphical compositing objects"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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

// These are GOB attribute and option flags.  They may need to be declared
// using the same method as node flags, so long as they are using their own
// memory pool.  (This is not the long term plan.)

#define GOBF_0_IS_TRUE \
    FLAGIT_LEFT(0) // aligns with NODE_FLAG_NODE

#define GOBF_1_IS_FALSE \
    FLAGIT_LEFT(1) // aligns with NODE_FLAG_FREE

#define GOBF_MARK \
    FLAGIT_LEFT(2)

#define GOBF_TOP \
    FLAGIT_LEFT(3) // Top level (window or output image)

#define GOBF_WINDOW \
    FLAGIT_LEFT(4) // Window (parent is OS window reference)

#define GOBF_OPAQUE \
    FLAGIT_LEFT(5) // Has no alpha

#define GOBF_STATIC \
    FLAGIT_LEFT(6) // Does not change
    
#define GOBF_HIDDEN \
    FLAGIT_LEFT(7) // Is hidden (e.g. hidden window)

#define GOBF_RESIZE \
    FLAGIT_LEFT(8) // Can be resized

#define GOBF_NO_TITLE \
    FLAGIT_LEFT(9) // Has window title

#define GOBF_NO_BORDER \
    FLAGIT_LEFT(10) // Has no window border

#define GOBF_DROPABLE \
    FLAGIT_LEFT(11) // Let window receive drag and drop

#define GOBF_TRANSPARENT \
    FLAGIT_LEFT(12) // Window is in transparent mode

#define GOBF_POPUP \
    FLAGIT_LEFT(13) // Window is a popup (with owner window)

#define GOBF_MODAL \
    FLAGIT_LEFT(14) // Modal event filtering

#define GOBF_ON_TOP \
    FLAGIT_LEFT(15) // The window is always on top

#define GOBF_ACTIVE \
    FLAGIT_LEFT(16) // Window is active

#define GOBF_MINIMIZE \
    FLAGIT_LEFT(17) // Window is minimized

#define GOBF_MAXIMIZE \
    FLAGIT_LEFT(18) // Window is maximized

#define GOBF_RESTORE \
    FLAGIT_LEFT(19) // Window is restored

#define GOBF_FULLSCREEN \
    FLAGIT_LEFT(20) // Window is fullscreen

#if defined(CPLUSPLUS_11)
    static_assert(20 < 32, "GOBF_XXX too high"); // 32 bits on 32 bit platform
#endif


enum GOB_STATE {        // GOB state flags
    GOBS_OPEN = 1 << 0, // Window is open
    GOBS_ACTIVE = 1 << 1, // Window is active
    GOBS_NEW = 1 << 2 // Gob is new to pane (old-offset, old-size wrong)
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

// These packed values for Rebol pairs are "X and Y coordinates" as "F"loat.
// (For PAIR! in Ren-C, actual pairing series are used, which
// can hold two values at full REBVAL precision (either integer or decimal)

typedef struct {
    float x;
    float y;
} REBXYF;


typedef struct rebol_gob REBGOB;

struct rebol_gob {
    struct Reb_Header header;

    REBCNT state;       // state flags

#ifdef REB_DEF
    REBSER *pane;       // List of child GOBs
#else
    void *pane;
#endif

    REBGOB *parent;     // Parent GOB (or window ptr)

    REBYTE alpha;       // transparency
    REBYTE ctype;       // content data type
    REBYTE dtype;       // pointer data type
    REBYTE resv;        // reserved

    REBGOB *owner;      // !!! was a singular item in a union

#ifdef REB_DEF
    REBSER *content;    // content value (block, string, color)
    REBSER *data;       // user defined data
#else
    void *content;
    void *data;
#endif

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
    cast(void, (g)->header.bits |= (f))
#define GET_GOB_FLAG(g,f) \
    LOGICAL((g)->header.bits & (f))
#define CLR_GOB_FLAG(g,f) \
    cast(void, (g)->header.bits &= ~(f))

#define SET_GOB_STATE(g,f) \
    cast(void, (g)->state |= (f))
#define GET_GOB_STATE(g,f) \
    LOGICAL((g)->state & (f))
#define CLR_GOB_STATE(g,f) \
    cast(void, (g)->state &= ~(f))

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
#define GOB_STRING(g)   RL_Gob_String(g)
#define GOB_LEN(g)      RL_Gob_Len(g)
#define GOB_HEAD(g)     RL_Gob_Head(g)
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

extern REBGOB *Gob_Root; // Top level GOB (the screen)

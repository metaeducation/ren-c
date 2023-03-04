//
//  File: %reb-event.h
//  Summary: "REBOL event definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Events are unusual for datatypes defined in extensions, because they use
// a pre-reserved REB_EVENT byte ID in the header to identify the cell type.
// This means they don't have to sacrifice the "EXTRA" uintptr_t field for
// the extension type identity, and can fit an entire event in one cell.
//
// EVENT EXTRA CONTAINS 4 BYTES
//
//     uint8_t type;   // event id (mouse-move, mouse-button, etc)
//     uint8_t flags;  // special flags
//     uint8_t win;    // window id
//     uint8_t model;  // port, object, gui, callback
//
// EVENT PAYLOAD CONTAINS 2 POINTER-SIZED THINGS
//
//     "eventee": REBSER* (port or object)
//     "data": "an x/y position or keycode (raw/decoded)"
//

#define REBEVT REBVAL


#define VAL_EVENT_TYPE(v) \
    cast(SymId, FIRST_UINT16(EXTRA(Any, (v)).u))

inline static void SET_VAL_EVENT_TYPE(REBVAL *v, SymId sym) {
    SET_FIRST_UINT16(EXTRA(Any, (v)).u, sym);
}

// 8-bit event flags (space is at a premium to keep events in a single cell)

enum {
    EVF_COPIED = 1 << 0,  // event data has been copied !!! REVIEW const abuse
    EVF_HAS_XY = 1 << 1,  // map-event will work on it
    EVF_DOUBLE = 1 << 2,  // double click detected
    EVF_CONTROL = 1 << 3,
    EVF_SHIFT = 1 << 4
};

#define EVF_MASK_NONE 0

#define VAL_EVENT_FLAGS(v) \
    THIRD_BYTE(EXTRA(Any, (v)).u)

#define mutable_VAL_EVENT_FLAGS(v) \
    mutable_THIRD_BYTE(EXTRA(Any, (v)).u)


//=//// EVENT NODE and "EVENT MODEL" //////////////////////////////////////=//
//
// Much of the single-cell event's space is used for flags, but it can store
// one pointer's worth of "eventee" data indicating the object that the event
// was for--the PORT!, GOB!, etc.
//
// (Note: R3-Alpha also had something called a "callback" which pointed the
// event to the "system.ports.callback port", but there seemed to be no uses.)
//
// In order to keep the core GC agnostic about events, if the pointer's slot
// is to something that needs to participate in GC behavior, it must be a
// Node* and the cell must be marked with CELL_FLAG_PAYLOAD_FIRST_IS_NODE.
//

enum {
    EVM_PORT,       // event holds port pointer
    EVM_OBJECT,     // event holds object context pointer
    EVM_GUI,        // GUI event uses system/view/event/port
    EVM_CALLBACK,   // Callback event uses system.ports.callback port
    EVM_MAX
};

#define VAL_EVENT_MODEL(v) \
    FOURTH_BYTE(EXTRA(Any, (v)).u)

#define mutable_VAL_EVENT_MODEL(v) \
    mutable_FOURTH_BYTE(EXTRA(Any, (v)).u)

#define VAL_EVENT_NODE(v) \
    VAL_NODE1(v)

#define SET_VAL_EVENT_NODE(v,p) \
    INIT_VAL_NODE1((v), (p))

#define VAL_EVENT_DATA(v) \
    PAYLOAD(Any, (v)).second.u

// Position event data.
//
// Note: There was a use of VAL_EVENT_XY() for optimized comparison.  This
// would violate strict aliasing, as you must read and write the same types,
// with the sole exception being char* access.  If the fields are assigned
// through uint16_t pointers, you can't read the aggregate with uint32_t.

#define VAL_EVENT_X(v) \
    FIRST_UINT16(VAL_EVENT_DATA(v))

inline static void SET_VAL_EVENT_X(REBVAL *v, uint16_t x) {
    SET_FIRST_UINT16(VAL_EVENT_DATA(v), x);
}

#define VAL_EVENT_Y(v) \
    SECOND_UINT16(VAL_EVENT_DATA(v))

inline static void SET_VAL_EVENT_Y(REBVAL *v, uint16_t y) {
    SET_SECOND_UINT16(VAL_EVENT_DATA(v), y);
}


// Key event data (Ren-C expands to use SYM_XXX for named keys; it would take
// an alternate/expanded cell format for EVENT! to store a whole String(*))
//
// Note: It appears the keycode was zeroed when a keysym was assigned, so you
// can only have one or the other.

#define VAL_EVENT_KEYSYM(v) \
    cast(SymId, FIRST_UINT16(VAL_EVENT_DATA(v)))

#define SET_VAL_EVENT_KEYSYM(v,keysym) \
    SET_FIRST_UINT16(VAL_EVENT_DATA(v), (keysym))

#define VAL_EVENT_KEYCODE(v) \
    SECOND_UINT16(VAL_EVENT_DATA(v))

#define SET_VAL_EVENT_KEYCODE(v,keycode) \
    SET_SECOND_UINT16(VAL_EVENT_DATA(v), (keycode))

// !!! These hooks allow the REB_EVENT cell type to dispatch to code in the
// EVENT! extension if it is loaded.
//
extern REBINT CT_Event(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict);
extern Bounce MAKE_Event(Frame(*) frame_, enum Reb_Kind kind, option(const REBVAL*) parent, const REBVAL *arg);
extern Bounce TO_Event(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *arg);
extern void MF_Event(REB_MOLD *mo, noquote(Cell(const*)) v, bool form);
extern REBTYPE(Event);

// !!! The port scheme is also being included in the extension.

extern Bounce Event_Actor(Frame(*) frame_, REBVAL *port, Symbol(const*) verb);
extern void Startup_Event_Scheme(void);
extern void Shutdown_Event_Scheme(void);


extern int64_t Delta_Time(int64_t base);

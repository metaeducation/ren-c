//
//  File: %reb-event.h
//  Summary: "REBOL event definitions"
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
// !!! The R3-Alpha host model and eventing system is generally deprecated
// in Ren-C, but is being kept working due to dependencies for R3/View.
//
// One change that was necessary in Ren-C was for payloads inside of REBVALs
// to be split into a 64-bit aligned portion, and a common 32-bit "extra"
// portion that would be 32-bit aligned on 32-bit platforms.  This change
// was needed in order to write a common member of a union without
// disengaging the rest of the payload.
//
// That required the Reb_Event--which was previously three 32-bit quantities,
// to split its payload up.  Now to get a complete event structure through
// the API, a full alias to a REBVAL is given.
//

#pragma pack(4)
struct Reb_Event {
    u8  type;       // event id (mouse-move, mouse-button, etc)
    u8  flags;      // special flags
    u8  win;        // window id
    u8  model;      // port, object, gui, callback
    u32 data;       // an x/y position or keycode (raw/decoded)
};

union Reb_Eventee {
    REBREQ *req; // request (for device events)
#ifdef REB_DEF
    REBSER *ser; // port or object
#else
    void *ser;
#endif
};

typedef struct {
    void *header;
    union Reb_Eventee eventee;
    u8 type;
    u8 flags;
    u8 win;
    u8 model;
    u32 data;
#if defined(__LP64__) || defined(__LLP64__)
    void *padding;
#endif
} REBEVT; // mirrors REBVAL holding a Reb_Event payload, should be compatible

// Note: the "eventee" series and the "request" live in the REBVAL
#pragma pack()

// Special event flags:
//
// !!! So long as events are directly hooking into the low-level REBVAL
// implementation, this could just use EVENT_FLAG_XXX flags.  eventee could
// be a binding to a REBNOD that was able to inspect that node to get the
// data "model".  

enum {
    EVF_COPIED = 1 << 0, // event data has been copied
    EVF_HAS_XY = 1 << 1, // map-event will work on it
    EVF_DOUBLE = 1 << 2, // double click detected
    EVF_CONTROL = 1 << 3,
    EVF_SHIFT = 1 << 4
};


// Event port data model

enum {
    EVM_DEVICE,     // I/O request holds the port pointer
    EVM_PORT,       // event holds port pointer
    EVM_OBJECT,     // event holds object context pointer
    EVM_GUI,        // GUI event uses system/view/event/port
    EVM_CALLBACK,   // Callback event uses system/ports/callback port
    EVM_MAX
};

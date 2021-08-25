//
//  File: %reb-device.h
//  Summary: "External REBOL Devices (OS Independent)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// !!! To do I/O, R3-Alpha had the concept of "simple" devices, which would
// represent abstractions of system services (Dev_Net would abstract the
// network layer, Dev_File the filesystem, etc.)
//
// There were a fixed list of commands these devices would handle (OPEN,
// CONNECT, READ, WRITE, CLOSE, QUERY).  Further parameterization was done
// with the fields of a specialized C structure called a REBREQ.
//
// This layer was code solely used by Rebol, and needed access to data
// resident in Rebol types.  For instance: if one is to ask to read from a
// file, it makes sense to use Rebol's FILE!.  And if one is reading into an
// existing BINARY! buffer, it makes sense to give the layer the BINARY!.
// But there was an uneasy situation of saying that these REBREQ could not
// speak in Rebol types, resulting in things like picking pointers out of
// the guts of Rebol cells and invoking unknown interactions with the GC by
// putting them into a C struct.
//
// Ren-C is shifting the idea to where a REBREQ is actually a REBARR, and
// able to hold full values (for starters, a REBSER* containing binary data
// of what used to be in a REBREQ...which is actually how PORT!s held a
// REBREQ in their state previously).
//


#define LINK_ReqNext_TYPE       REBREQ*  // alias for REBBIN
#define LINK_ReqNext_CAST       BIN
#define HAS_LINK_ReqNext        FLAVOR_BINARY

#define MISC_ReqPortCtx_TYPE    REBCTX*
#define MISC_ReqPortCtx_CAST    CTX
#define HAS_MISC_ReqPortCtx     FLAVOR_BINARY

enum Reb_Device_Command {
    RDC_OPEN,       // open device unit (port)
    RDC_CLOSE,      // close device unit

    RDC_READ,       // read from unit
    RDC_WRITE,      // write to unit

    RDC_CONNECT,    // connect (in or out)

    RDC_QUERY,      // query unit info

    RDC_CREATE,     // create unit target
    RDC_DELETE,     // delete unit target
    RDC_RENAME,
    RDC_LOOKUP,
    RDC_MAX
};

// Device Request (Command) Return Codes:
#define DR_PEND   1 // request is still pending
#define DR_DONE   0 // request is complete w/o errors

// REBOL Device Flags and Options (bitnums):
enum {
    RDF_OPEN = 1 << 1, // Global open (for devs that cannot multi-open)

    // !!! There used to be something here called "RDO_AUTO_POLL" which said
    // "Poll device, even if no requests (e.g. interrupts)".  There were no
    // instances.  If someone needed to accomplish this, they could just put
    // in a request that never says it's done, but keeps asking to be left
    // in the pending queue.
};

// REBOL Request Flags (bitnums):
enum {
    RRF_OPEN = 1 << 0, // Port is open
    RRF_DONE = 1 << 1, // Request is done (used when extern proc changes it)
    RRF_FLUSH = 1 << 2, // Flush WRITE
//  RRF_PREWAKE,    // C-callback before awake happens (to update port object)
    RRF_PENDING = 1 << 3, // Request is attached to pending list
    RRF_ACTIVE = 1 << 5, // Port is active, even no new events yet
};


// RFM - REBOL File Modes
enum {
    RFM_READ = 1 << 0,
    RFM_WRITE = 1 << 1,
    RFM_APPEND = 1 << 2,
    RFM_SEEK = 1 << 3,
    RFM_NEW = 1 << 4,
    RFM_READONLY = 1 << 5,
    RFM_TRUNCATE = 1 << 6,
    RFM_RESEEK = 1 << 7, // file index has moved, reseek
    RFM_DIR = 1 << 8,
    RFM_TEXT = 1 << 9 // on appropriate platforms, translate LF to CR LF
};

#define MAX_FILE_NAME 1022


// Commands:
typedef int32_t (*DEVICE_CMD_CFUNC)(REBREQ *req);
#define DEVICE_CMD int32_t // Used to define

// Device structure:
struct rebol_device {
    const char *title;      // title of device
    uint32_t version;       // version, revision, release
    uint32_t date;          // year, month, day, hour
    DEVICE_CMD_CFUNC *commands; // command dispatch table
    uint32_t max_command;   // keep commands in bounds
    uint32_t req_size;      // size of the request state
    REBNOD *pending;        // pending requests (actually a REBREQ*)
    uint32_t flags;         // state: open, signal

    REBDEV *next;  // next in linked list of registered devices
};

// Inializer (keep ordered same as above)
#define DEFINE_DEV(w,t,v,c,m,s) \
    REBDEV w = {t, v, 0, c, m, s, 0, 0, 0}

// Request structure:       // Allowed to be extended by some devices
struct rebol_devreq {

    REBDEV *device;
    union {
        void *handle;       // OS object
        int socket;         // OS identifier
        int id;
    } requestee;

    enum Reb_Device_Command command;  // command code

    uint32_t modes;         // special modes, types or attributes
    uint16_t flags;         // request flags
    uint16_t state;         // device process flags
    int32_t timeout;        // request timeout
//  int (*prewake)(void *); // callback before awake

    // !!! Only one of these fields is active at a time, so what it really
    // represents is a union.  A struct helps catch errors while it is being
    // untangled.  Ultimately what this would evolve into would just be a
    // REBVAL*, as this becomes a more Rebol-aware and "usermode" concept.
    //
    struct {
        unsigned char *data;
        REBVAL *binary;  // !!! outlives the rebreq (on stack or in port_ctx)
    } common;
    size_t length;  // length to transfer
    size_t actual;  // length actually transferred
};


#if defined(NDEBUG)
    #define ASSERT_REBREQ(req) \
        NOOP
#else
    inline static void ASSERT_REBREQ(REBREQ *req) {  // basic sanity check
        assert(BIN_LEN(req) >= sizeof(struct rebol_devreq));
        assert(GET_SERIES_FLAG(req, LINK_NODE_NEEDS_MARK));
        assert(GET_SERIES_FLAG(req, MISC_NODE_NEEDS_MARK));
    }
#endif

inline static struct rebol_devreq *Req(REBREQ *req) {
    ASSERT_REBREQ(req);
    return cast(struct rebol_devreq*, BIN_HEAD(req));
}



// !!! Transitional - Lifetime management of REBREQ in R3-Alpha was somewhat
// unclear, with them being created sometimes on the stack, and sometimes
// linked into a pending list if a request turned out to be synchronous and
// not need the request to live longer.  To try and design for efficiency,
// Append_Request() currently is the only place that manages the request for
// asynchronous handling...other clients are expected to free.
//
// !!! Some requests get Append_Request()'d multiple times, apparently.
// Review the implications, but just going with making it legal to manage
// something multiple times for now.
//
inline static void Force_Req_Managed(REBREQ *req) {
    ASSERT_REBREQ(req);
    Force_Series_Managed(req);
}


inline static void Free_Req(REBREQ *req) {
    ASSERT_REBREQ(req);
    Free_Unmanaged_Series(req);
}


// Reb_Device_Command is not available in %tmp-internals.h, so we use this
// inline function to put it into the request and call the device (that's
// what it did anyway.)
//
inline static REBVAL *OS_DO_DEVICE(
    REBREQ *req,
    enum Reb_Device_Command command
){
    Req(req)->command = command;
    return OS_Do_Device(req);
}


// Convenience routine that wraps OS_DO_DEVICE for simple requests.
//
// !!! Because the device layer is deprecated, the relevant inelegance of
// this is not particularly important...more important is that the API
// handles and error mechanism works.
//
inline static void OS_DO_DEVICE_SYNC(
    REBREQ *req,
    enum Reb_Device_Command command
){
    REBVAL *result = OS_DO_DEVICE(req, command);
    assert(result != NULL);  // should be synchronous
    if (rebDid("error?", result))
        rebJumps("fail", result);
    rebRelease(result);  // ignore result
}

//
//  File: %reb-device.h
//  Summary: "External REBOL Devices (OS Independent)"
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
// Critical: all struct alignment must be 4 bytes (see compile options)
//

#ifdef HAS_POSIX_SIGNAL
#include <signal.h>
#endif

#include <assert.h>
#include "assert-fixes.h"

// REBOL Device Identifiers:
// Critical: Must be in same order as Device table in host-device.c
enum {
    RDI_SYSTEM,
    RDI_STDIO,
    RDI_CONSOLE,
    RDI_FILE,
    RDI_EVENT,
    RDI_NET,
    RDI_DNS,
    RDI_CLIPBOARD,
    RDI_SERIAL,
#ifdef HAS_POSIX_SIGNAL
    RDI_SIGNAL,
#endif
    RDI_MAX,
    RDI_LIMIT = 32
};


// REBOL Device Commands:
enum {
    RDC_INIT,       // init device driver resources
    RDC_QUIT,       // cleanup device driver resources

    RDC_OPEN,       // open device unit (port)
    RDC_CLOSE,      // close device unit

    RDC_READ,       // read from unit
    RDC_WRITE,      // write to unit

    RDC_POLL,       // check for activity
    RDC_CONNECT,    // connect (in or out)

    RDC_QUERY,      // query unit info
    RDC_MODIFY,     // set modes (also get modes)

    RDC_CREATE,     // create unit target
    RDC_DELETE,     // delete unit target
    RDC_RENAME,
    RDC_LOOKUP,
    RDC_MAX,

    RDC_CUSTOM=32   // start of custom commands
};

// Device Request (Command) Return Codes:
#define DR_PEND   1 // request is still pending
#define DR_DONE   0 // request is complete w/o errors
#define DR_ERROR -1 // request had an error

// REBOL Device Flags and Options (bitnums):
enum {
    // Status flags:
    RDF_INIT = 1 << 0, // Device is initialized
    RDF_OPEN = 1 << 1, // Global open (for devs that cannot multi-open)
    // Options:
    RDO_MUST_INIT = 1 << 2, // Do not allow auto init (manual init required)
    RDO_AUTO_POLL = 1 << 3 // Poll device, even if no requests (e.g. interrupts)
};

// REBOL Request Flags (bitnums):
enum {
    RRF_OPEN = 1 << 0, // Port is open
    RRF_DONE = 1 << 1, // Request is done (used when extern proc changes it)
    RRF_FLUSH = 1 << 2, // Flush WRITE
//  RRF_PREWAKE,    // C-callback before awake happens (to update port object)
    RRF_PENDING = 1 << 3, // Request is attached to pending list
    RRF_ALLOC = 1 << 4, // Request is allocated, not a temp on stack
    RRF_WIDE = 1 << 5, // Wide char IO
    RRF_ACTIVE = 1 << 6, // Port is active, even no new events yet

    // !!! This was a "local flag to mark null device" which when not managed
    // here was confusing.  Given the need to essentially replace the whole
    // device model, it's clearer to keep it here.
    //
    SF_DEV_NULL = 1 << 31
};

// REBOL Device Errors:
enum {
    RDE_NONE,
    RDE_NO_DEVICE,  // command did not provide device
    RDE_NO_COMMAND, // command past end
    RDE_NO_INIT,    // device has not been inited
    RDE_MAX
};

enum {
    RDM_NULL = 1 << 0 // !!! "Null device", can this just be a boolean?
};

// Serial Parity
enum {
    SERIAL_PARITY_NONE,
    SERIAL_PARITY_ODD,
    SERIAL_PARITY_EVEN
};

// Serial Flow Control
enum {
    SERIAL_FLOW_CONTROL_NONE,
    SERIAL_FLOW_CONTROL_HARDWARE,
    SERIAL_FLOW_CONTROL_SOFTWARE
};

// Forward references:
typedef struct rebol_device REBDEV;
typedef struct rebol_devreq REBREQ;

// Commands:
typedef int32_t (*DEVICE_CMD_FUNC)(REBREQ *req);
#define DEVICE_CMD int32_t // Used to define

// Device structure:
struct rebol_device {
    const char *title;      // title of device
    uint32_t version;       // version, revision, release
    uint32_t date;          // year, month, day, hour
    DEVICE_CMD_FUNC *commands; // command dispatch table
    uint32_t max_command;   // keep commands in bounds
    uint32_t req_size;      // size of the request state
    REBREQ *pending;        // pending requests
    uint32_t flags;         // state: open, signal
};

// Inializer (keep ordered same as above)
#define DEFINE_DEV(w,t,v,c,m,s) \
    EXTERN_C REBDEV w; \
    REBDEV w = {t, v, 0, c, m, s, 0, 0}

// Request structure:       // Allowed to be extended by some devices
struct rebol_devreq {

    // Linkages:
    uint32_t device;        // device id (dev table)
    REBREQ *next;           // linked list (pending or done lists)
    void *port;             // link back to REBOL port object
    union {
        void *handle;       // OS object
        int socket;         // OS identifier
        int id;
    } requestee;            // !!! REVIEW: Not always "receiver"?  The name is
                            // "bad" (?) but at least unique, making it easy
                            // to change.  See also Reb_Event->eventee

    // Command info:
    int32_t command;        // command code
    int32_t error;          // error code
    uint32_t modes;         // special modes, types or attributes
    uint16_t flags;         // request flags
    uint16_t state;         // device process flags
    int32_t timeout;        // request timeout
//  int (*prewake)(void *); // callback before awake

    // Common fields:
    union {
        REBYTE *data;       // data to transfer
        REBREQ *sock;       // temp link to related socket
    } common;
    uint32_t length;        // length to transfer
    uint32_t actual;        // length actually transferred
};

#define AS_REBREQ(req) (&(req)->devreq)

#ifdef HAS_POSIX_SIGNAL
struct devreq_posix_signal {
    struct rebol_devreq devreq;
    sigset_t mask;      // signal mask
};

#if !defined(NDEBUG)
#define DEVREQ_POSIX_SIGNAL(req) (assert(req->device == RDI_SIGNAL), cast(struct devreq_posix_signal*, req))
#else
#define DEVREQ_POSIX_SIGNAL(req) cast(struct devreq_posix_signal*, req)
#endif
#endif

// !!! Hack used for making a 64-bit value as a struct, which works in
// 32-bit modes.  64 bits, even in 32 bit mode.  Based on the deprecated idea
// that "devices" would not have access to Rebol datatypes, and hence would
// not be able to communicate with Rebol directly with a TIME! or DATE!.
// To be replaced.
//
// (Note: compatible with FILETIME used in Windows)
//
#pragma pack(4)
typedef struct sInt64 {
    int32_t l;
    int32_t h;
} FILETIME_DEVREQ;
#pragma pack()

struct devreq_file {
    struct rebol_devreq devreq;
    REBCHR *path;           // file string (in OS local format)
    int64_t size;           // file size
    int64_t index;          // file index position
    FILETIME_DEVREQ time;   // file modification time (struct)
};

struct devreq_net {
    struct rebol_devreq devreq;
    uint32_t local_ip;      // local address used
    uint32_t local_port;    // local port used
    uint32_t remote_ip;     // remote address
    uint32_t remote_port;   // remote port
    void *host_info;        // for DNS usage
};

struct devreq_serial {
    struct rebol_devreq devreq;
    REBCHR *path;           //device path string (in OS local format)
    void *prior_attr;       // termios: retain previous settings to revert on close
    int32_t baud;           // baud rate of serial port
    uint8_t data_bits;      // 5, 6, 7 or 8
    uint8_t parity;         // odd, even, mark or space
    uint8_t stop_bits;      // 1 or 2
    uint8_t flow_control;   // hardware or software
};

inline static struct devreq_file* DEVREQ_FILE(struct rebol_devreq *req) {
    assert(req->device == RDI_FILE);
    return cast(struct devreq_file*, req);
}

inline static struct devreq_net *DEVREQ_NET(struct rebol_devreq *req) {
    assert(req->device == RDI_NET || req->device == RDI_DNS);
    return cast(struct devreq_net*, req);
}

inline static struct devreq_serial *DEVREQ_SERIAL(struct rebol_devreq *req) {
    assert(req->device == RDI_SERIAL);
    return cast(struct devreq_serial*, req);
}

#define OS_ENA -1
#define OS_EINVAL -2
#define OS_EPERM -3
#define OS_ESRCH -4

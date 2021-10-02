//
//  File: %reb-net.h
//  Summary: "Network device definitions"
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

EXTERN_C REBDEV *Dev_Net;

enum Transport_Type {
    TRANSPORT_TCP,
    TRANSPORT_UDP
};

// Note that 0 is technically a legal result from connect() as a socket ID.
//
#define SOCKET_NONE -1


// backlog queue – the maximum length of queued connections for uv_listen()
// (this number is what was used in libuv's echo.c example, SOMAXCONN was used
// in historical Rebol)
//
#define DEFAULT_BACKLOG 1024


enum Reb_Socket_Modes {
    RSM_ATTEMPT = 1 << 1,   // attempting connection
    RSM_BIND    = 1 << 3,   // socket is bound to port
    RSM_LISTEN  = 1 << 4,   // socket is listening (TCP)

    RST_LISTEN  = 1 << 8    // signals the socket should listen when opened? :-/
};

#define IPA(a,b,c,d) (a<<24 | b<<16 | c<<8 | d)

// This is the state information that is stored in a network PORT!'s `state`
// field.  It is a BINARY! whose bytes hold this C struct.
//
struct Reb_Sock_Port_State {
    enum Transport_Type transport;  // TCP or UDP

    // To tell if a socket had been opened and possibly connected, R3-Alpha
    // used RSM_OPEN and RSM_CONNECTED flags.  But to make it self-checking,
    // we put the information in the socket handle itself, and use -1 as an
    // illegal socket...to catch uses that don't check the flags.  Both fd
    // (file descriptor) and socket are set to the same thing when open and
    // connected...but if the socket is only open and not connected then the
    // fd will be set and the socket will be -1.
    //
    uv_tcp_t tcp;
    uv_stream_t* stream;

    int fd;  // file descriptor; -1 indicates closed, other value is open

    uint32_t modes;  // RSM_XXX flags

    uint32_t local_ip;
    uint32_t local_port_number;
    uint32_t remote_ip;
    uint32_t remote_port_number;
};

typedef struct Reb_Sock_Port_State SOCKREQ;

inline static SOCKREQ *Sock_Of_Port(const REBVAL *port)
{
    REBVAL *state = CTX_VAR(VAL_CONTEXT(port), STD_PORT_STATE);
    return cast(SOCKREQ*, VAL_BINARY_AT_ENSURE_MUTABLE(state));
}


typedef struct {
    uv_write_t req;  // make first member of struct so we can cast the address

    REBCTX *port_ctx;
    REBVAL *binary;
} Reb_Write_Request;


typedef struct {
    REBCTX *port_ctx;

    ssize_t length;  // length to transfer (or -1 for UNLIMITED)
    size_t actual;  // length actually transferred

    // !!! the binary is assumed to just live in the port's "data", this
    // prevents multiple in-flight reads and is a design flaw, but translating
    // the R3-Alpha code for now just as a first step.

} Reb_Read_Request;

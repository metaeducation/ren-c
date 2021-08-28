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
    int fd;  // file descriptor; -1 indicates closed, other value is open
    int socket;  // if connected, same as fd; -1 if disconnected

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

enum Reb_Transfer_Direction {
    TRANSFER_SEND,
    TRANSFER_RECEIVE
};

struct Reb_Sock_Transfer {
    REBCTX *port_ctx;
    enum Reb_Transfer_Direction direction;

    // !!! For the moment, only the binary data for TRANSFER_SEND is stored in
    // the transfer structure.  The data for TRANSFER_RECEIVE is stored in
    // the port data.  This is because the EVENT! datatype tried to compress
    // all its information into one cell, hence it cannot carry both who to
    // notify and what to notify with.  So the port only knows "you are
    // finished reading", and looks to itself for the buffer.
    //
    // Paint a picture toward a better future by at least putting the total
    // length and how much has actually been transferred so far here, and the
    // binary is managed here for SEND.
    //
    REBVAL *binary;

    size_t length;  // length to transfer
    size_t actual;  // length actually transferred

    struct Reb_Sock_Transfer *next;
};

struct Reb_Sock_Listener {
    REBCTX *port_ctx;

    struct Reb_Sock_Listener *next;
};

struct Reb_Sock_Connector {
    REBCTX *port_ctx;

    struct Reb_Sock_Connector *next;
};

extern struct Reb_Sock_Transfer *Net_Transfers;
extern struct Reb_Sock_Listener *Net_Listeners;
extern struct Reb_Sock_Connector *Net_Connectors;

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


// REBOL Socket Modes (state flags)
enum {
    RSM_OPEN    = 1 << 0,   // socket is allocated
    RSM_ATTEMPT = 1 << 1,   // attempting connection
    RSM_CONNECT = 1 << 2,   // connection is open
    RSM_BIND    = 1 << 3,   // socket is bound to port
    RSM_LISTEN  = 1 << 4,   // socket is listening (TCP)
    RSM_ACCEPT  = 1 << 7,   // an inbound connection

    RST_LISTEN  = 1 << 8    // signals the socket should listen when opened? :-/
};

#define IPA(a,b,c,d) (a<<24 | b<<16 | c<<8 | d)

struct devreq_net {
    int socket;         // OS identifier
    enum Transport_Type transport;  // TCP or UDP

    uint32_t modes;         // special modes, types or attributes

    uint32_t local_ip;      // local address used
    uint32_t local_port_number;    // local port used
    uint32_t remote_ip;     // remote address
    uint32_t remote_port_number;   // remote port
    void *host_info;        // for DNS usage

    REBVAL *binary;  // !!! outlives the Reb_Transfer for receives
};

typedef struct devreq_net SOCKREQ;

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

    // !!! For the moment, only the binary data for TRANFER_SEND is stored in
    // the transfer structure.  The data for TRANSFER_RECEIVE is stored in
    // the "SOCKREQ" so the port can get at it.  This is because the EVENT!
    // datatype tried to compress all its information into one cell, hence it
    // cannot carry both who to notify and what to notify with.  So the
    // port only knows "you are finished reading".
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

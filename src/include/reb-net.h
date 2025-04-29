//
//  file: %reb-net.h
//  summary: "Network device definitions"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

// REBOL Socket types:
enum socket_types {
    RST_UDP     = 1 << 0,   // TCP or UDP
    RST_LISTEN  = 1 << 8,   // LISTEN
    RST_REVERSE = 1 << 9    // DNS reverse
};

// REBOL Socket Modes (state flags)
enum {
    RSM_OPEN    = 1 << 0,   // socket is allocated
    RSM_ATTEMPT = 1 << 1,   // attempting connection
    RSM_CONNECT = 1 << 2,   // connection is open
    RSM_BIND    = 1 << 3,   // socket is bound to port
    RSM_LISTEN  = 1 << 4,   // socket is listening (TCP)
    RSM_SEND    = 1 << 5,   // sending
    RSM_RECEIVE = 1 << 6,   // receiving
    RSM_ACCEPT  = 1 << 7    // an inbound connection
};

#define IPA(a,b,c,d) (a<<24 | b<<16 | c<<8 | d)

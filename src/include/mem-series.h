//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2015 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
//  Summary: Low level memory-oriented access routines for series
//  File: %mem-series.h
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These are implementation details of series that most code should not need
// to use.
//

// The pooled allocator for REBSERs has an enumeration function where all
// nodes can be visited, and this is used by the garbage collector.  This
// includes nodes that have never been allocated or have been freed, so
// "in-band" inside the REBSER there must be some way to tell if a node is
// live or not.
//
// When the pool is initially allocated it is memset() to zero, hence the
// signal must be some field or bit being zero that is not otherwise used.
// The signal currently used is the "width" being zero.  The only downside
// of this is that it means the sizes range from 1-255, whereas if 0 was
// available the width could always be incremented by 1 and range 1-256.
//
#define SER_FREED(s)  (0 == SER_WIDE(s))

//
// Non-series-internal code needs to read SER_WIDE but should not be
// needing to set it directly.
//
// !!! Can't `assert((w) < MAX_SERIES_WIDE)` without triggering "range of
// type makes this always false" warning; C++ build could sense if it's a
// REBYTE and dodge the comparison if so.
//

#define MAX_SERIES_WIDE 0x100 \

#define SER_SET_WIDE(s,w) \
    ((s)->info.bits = ((s)->info.bits & 0xffff) | (w << 16))


//
// Bias is empty space in front of head:
//

#define SER_BIAS(s) \
    cast(REBCNT, ((s)->content.dynamic.bias >> 16) & 0xffff)

#define MAX_SERIES_BIAS 0x1000 \

#define SER_SET_BIAS(s,b) \
    ((s)->content.dynamic.bias = \
        ((s)->content.dynamic.bias & 0xffff) | (b << 16))

#define SER_ADD_BIAS(s,b) \
    ((s)->content.dynamic.bias += (b << 16))

#define SER_SUB_BIAS(s,b) \
    ((s)->content.dynamic.bias -= (b << 16))

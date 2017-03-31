//
//  File: %mod-jpg.c
//  Summary: "JPEG codec natives (dependent on %sys-core.h)"
//  Section: Extension
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
// The original JPEG encoder and decoder did not include sys-core.h.   But
// after getting rid of the REBCDI-based interface and converting codecs to
// be natives, it's necessary to include the core.
//

#include "sys-core.h"
#include "sys-ext.h"
#include "tmp-mod-jpg-first.h"

// These routines live in %u-jpg.c, which doesn't depend on %sys-core.h, but
// has a minor dependency on %reb-c.h

extern jmp_buf jpeg_state;
extern void jpeg_info(char *buffer, int nbytes, int *w, int *h);
extern void jpeg_load(char *buffer, int nbytes, char *output);


//
//  identify-jpeg?: native [
//
//  {Codec for identifying BINARY! data for a JPEG}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_jpeg_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_JPEG_Q;

    // Handle JPEG error throw:
    if (setjmp(jpeg_state)) {
        return R_FALSE;
    }

    REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    int w, h;
    jpeg_info(s_cast(data), len, &w, &h); // may longjmp above
    return R_TRUE;
}


//
//  decode-jpeg: native [
//
//  {Codec for decoding BINARY! data for a JPEG}
//
//      return: [image!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_jpeg)
{
    INCLUDE_PARAMS_OF_DECODE_JPEG;

    // Handle JPEG error throw:
    if (setjmp(jpeg_state)) {
        fail (Error_Bad_Media_Raw()); // generic
    }

    REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    int w, h;
    jpeg_info(s_cast(data), len, &w, &h); // may longjmp above

    REBSER *ser = Make_Image(w, h, TRUE);
    jpeg_load(s_cast(data), len, cast(char*, IMG_DATA(ser)));

    Init_Image(D_OUT, ser);
    return R_OUT;
}

#include "tmp-mod-jpg-last.h"

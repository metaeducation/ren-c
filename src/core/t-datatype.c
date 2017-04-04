//
//  File: %t-datatype.c
//  Summary: "datatype datatype"
//  Section: datatypes
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

#include "sys-core.h"


//
//  CT_Datatype: C
//
REBINT CT_Datatype(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0) return (VAL_TYPE_KIND(a) == VAL_TYPE_KIND(b));
    return -1;
}


//
//  MAKE_Datatype: C
//
void MAKE_Datatype(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    if (!IS_WORD(arg))
        fail (Error_Bad_Make(kind, arg));

    REBSYM sym = VAL_WORD_SYM(arg);
    if (sym == SYM_0 || sym > SYM_FROM_KIND(REB_MAX))
        fail (Error_Bad_Make(kind, arg));

    Reset_Val_Header(out, REB_DATATYPE);
    VAL_TYPE_KIND(out) = KIND_FROM_SYM(sym);
    VAL_TYPE_SPEC(out) = 0;
}


//
//  TO_Datatype: C
//
void TO_Datatype(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    MAKE_Datatype(out, kind, arg);
}


//
//  REBTYPE: C
//
REBTYPE(Datatype)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARG(2);
    enum Reb_Kind kind = VAL_TYPE_KIND(value);

    switch (action) {

    case SYM_REFLECT: {
        REBSYM sym = VAL_WORD_SYM(arg);
        if (sym == SYM_SPEC) {
            //
            // The "type specs" were loaded as an array, but this reflector
            // wants to give back an object.  Combine the array with the
            // standard object that mirrors its field order.
            //
            REBCTX *context = Copy_Context_Shallow(
                VAL_CONTEXT(Get_System(SYS_STANDARD, STD_TYPE_SPEC))
            );
            MANAGE_ARRAY(CTX_VARLIST(context));

            assert(CTX_TYPE(context) == REB_OBJECT);

            REBVAL *var = CTX_VARS_HEAD(context);
            REBVAL *key = CTX_KEYS_HEAD(context);

            // !!! Account for the "invisible" self key in the current
            // stop-gap implementation of self, still default on MAKE OBJECT!s
            //
            assert(VAL_KEY_SYM(key) == SYM_SELF);
            ++key; ++var;

            RELVAL *value = ARR_HEAD(
                VAL_TYPE_SPEC(CTX_VAR(Lib_Context, SYM_FROM_KIND(kind)))
            );

            for (; NOT_END(var); ++var, ++key) {
                if (IS_END(value)) Init_Blank(var);
                else {
                    // typespec array does not contain relative values
                    //
                    Derelativize(var, value, SPECIFIED);
                    ++value;
                }
            }

            Init_Object(D_OUT, context);
        }
        else
            fail (Error_Cannot_Reflect(VAL_TYPE(value), arg));
        break;}

    default:
        fail (Error_Illegal_Action(REB_DATATYPE, action));
    }

    return R_OUT;
}

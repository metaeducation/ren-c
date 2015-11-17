/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  t-function.c
**  Summary: function related datatypes
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

static REBOOL Same_Func(REBVAL *val, REBVAL *arg)
{
    if (VAL_TYPE(val) == VAL_TYPE(arg) &&
        VAL_FUNC_SPEC(val) == VAL_FUNC_SPEC(arg) &&
        VAL_FUNC_PARAMLIST(val) == VAL_FUNC_PARAMLIST(arg) &&
        VAL_FUNC_CODE(val) == VAL_FUNC_CODE(arg)) return TRUE;
    return FALSE;
}


//
//  CT_Function: C
//
REBINT CT_Function(REBVAL *a, REBVAL *b, REBINT mode)
{
    if (mode >= 0) return Same_Func(a, b);
    return -1;
}


//
//  As_Typesets: C
//
REBSER *As_Typesets(REBSER *types)
{
    REBVAL *val;

    types = Copy_Array_At_Shallow(types, 1);
    for (val = BLK_HEAD(types); NOT_END(val); val++) {
        SET_TYPE(val, REB_TYPESET);
    }
    return types;
}


//
//  MT_Function: C
// 
// For REB_FUNCTION and REB_CLOSURE "make spec", there is a function spec
// block and then a block of Rebol code implementing that function.  In that
// case we expect that `def` should be:
// 
//     [[spec] [body]]
// 
// With REB_COMMAND, the code is implemented via a C DLL, under a system of
// APIs that pre-date Rebol's open sourcing and hence Ren/C:
// 
//     [[spec] extension command-num]
// 
// See notes in Make_Command() regarding that mechanism and meaning.
//
REBFLG MT_Function(REBVAL *out, REBVAL *def, enum Reb_Kind type)
{
    REBVAL *spec;
    REBCNT len;

    if (!IS_BLOCK(def)) return FALSE;

    len = VAL_LEN(def);
    if (len < 2) return FALSE;

    spec = VAL_BLK_HEAD(def);

    if (!IS_BLOCK(def)) return FALSE;

    if (type == REB_COMMAND) {
        REBVAL *extension = VAL_BLK_SKIP(def, 1);
        REBVAL *command_num;

        if (len != 3) return FALSE;
        command_num = VAL_BLK_SKIP(def, 2);

        Make_Command(out, spec, extension, command_num);
    }
    else if (type == REB_FUNCTION || type == REB_CLOSURE) {
        REBVAL *body = VAL_BLK_SKIP(def, 1);

        // Spec-constructed functions do *not* have definitional returns
        // added automatically.  They are part of the generators.

        REBFLG has_return = FALSE;

        if (len != 2) return FALSE;

        Make_Function(out, type, spec, body, has_return);
    }
    else
        return FALSE;


    // We only get here if neither Make() raises an error...
    return TRUE;
}


//
//  REBTYPE: C
//
REBTYPE(Function)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;

    switch (action) {
    case A_TO:
        // `to function! foo` is meaningless (and should not be given meaning,
        // because `to function! [print "DOES exists for this, for instance"]`
        break;

    case A_MAKE:
        if (!IS_DATATYPE(value)) fail (Error_Invalid_Arg(value));

        // MT_Function checks for `[[spec] [body]]` arg if function/closure
        // and for `[[spec] extension command-num]` if command
        if (!MT_Function(D_OUT, arg, VAL_TYPE_KIND(value)))
            fail (Error_Bad_Make(VAL_TYPE_KIND(value), arg));
        return R_OUT;

    case A_COPY:
        // !!! The R3-Alpha theory was that functions could modify "their
        // bodies" while running, effectively accruing state that one might
        // want to snapshot.  See notes on Clonify_Function about why this
        // idea may be incorrect.
        *D_OUT = *value;
        Clonify_Function(D_OUT);
        return R_OUT;

    case A_REFLECT:
        switch (What_Reflector(arg)) {
        case OF_WORDS:
            Val_Init_Block(D_OUT, List_Func_Words(value));
            return R_OUT;

        case OF_BODY: {
            switch (VAL_TYPE(value))
            case REB_FUNCTION:
            case REB_CLOSURE: {
                // BODY-OF is an example of user-facing code that needs to be
                // complicit in the "lie" about the effective bodies of the
                // functions made by the optimized generators FUNC and CLOS...

                REBFLG is_fake;
                REBSER *body = Get_Maybe_Fake_Func_Body(&is_fake, value);
                Val_Init_Block(D_OUT, Copy_Array_Deep_Managed(body));

                if (VAL_TYPE(value) == REB_CLOSURE) {
                    // See #2221 for why closure body copies unbind locals
                    Unbind_Values_Core(
                        VAL_BLK_HEAD(D_OUT), VAL_FUNC_PARAMLIST(value), TRUE
                    );
                }
                if (is_fake) Free_Series(body); // was shallow copy
                return R_OUT;
            }

            case REB_NATIVE:
            case REB_COMMAND:
            case REB_ACTION:
                return R_NONE;
            }
            break;

        case OF_SPEC:
            Val_Init_Block(
                D_OUT, Copy_Array_Deep_Managed(VAL_FUNC_SPEC(value))
            );
            Unbind_Values_Deep(VAL_BLK_HEAD(value));
            return R_OUT;

        case OF_TYPES:
            Val_Init_Block(D_OUT, As_Typesets(VAL_FUNC_PARAMLIST(value)));
            return R_OUT;

        case OF_TITLE:
            arg = BLK_HEAD(VAL_FUNC_SPEC(value));
            while (NOT_END(arg) && !IS_STRING(arg) && !IS_WORD(arg))
                arg++;
            if (!IS_STRING(arg)) return R_NONE;
            Val_Init_String(D_OUT, Copy_Sequence(VAL_SERIES(arg)));
            return R_OUT;

        default:
            fail (Error_Cannot_Reflect(VAL_TYPE(value), arg));
        }
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), action));
}

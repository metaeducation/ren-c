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
REBINT CT_Datatype(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0) return (VAL_TYPE_KIND(a) == VAL_TYPE_KIND(b));
    return -1;
}


//
//  MAKE_Datatype: C
//
Bounce MAKE_Datatype(Value* out, enum Reb_Kind kind, const Value* arg) {
    if (Is_Word(arg)) {
        Option(SymId) sym = Cell_Word_Id(arg);
        if (not sym or (unwrap sym >= SYM_FROM_KIND(REB_MAX)))
            goto bad_make;

        return Init_Datatype(out, KIND_FROM_SYM(unwrap sym));
    }

  bad_make:;
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Datatype: C
//
Bounce TO_Datatype(Value* out, enum Reb_Kind kind, const Value* arg) {
    return MAKE_Datatype(out, kind, arg);
}


//
//  MF_Datatype: C
//
void MF_Datatype(REB_MOLD *mo, const Cell* v, bool form)
{
    Symbol* name = Canon(VAL_TYPE_SYM(v));
    if (form)
        Emit(mo, "N", name);
    else
        Emit(mo, "+DN", SYM_DATATYPE_X, name);
}


//
//  REBTYPE: C
//
REBTYPE(Datatype)
{
    Value* value = D_ARG(1);
    Value* arg = D_ARG(2);
    enum Reb_Kind kind = VAL_TYPE_KIND(value);

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        Option(SymId) sym = Cell_Word_Id(arg);
        if (sym == SYM_SPEC) {
            //
            // The "type specs" were loaded as an array, but this reflector
            // wants to give back an object.  Combine the array with the
            // standard object that mirrors its field order.
            //
            VarList* context = Copy_Context_Shallow_Managed(
                Cell_Varlist(Get_System(SYS_STANDARD, STD_TYPE_SPEC))
            );

            assert(CTX_TYPE(context) == REB_OBJECT);

            Value* var = Varlist_Slots_Head(context);
            Value* key = Varlist_Keys_Head(context);

            // !!! Account for the "invisible" self key in the current
            // stop-gap implementation of self, still default on MAKE OBJECT!s
            //
            assert(Key_Id(key) == SYM_SELF);
            ++key; ++var;

            Cell* item = Array_Head(
                VAL_TYPE_SPEC(Varlist_Slot(Lib_Context, SYM_FROM_KIND(kind)))
            );

            for (; NOT_END(var); ++var, ++key) {
                if (IS_END(item))
                    Init_Blank(var);
                else {
                    // typespec array does not contain relative values
                    //
                    Derelativize(var, item, SPECIFIED);
                    ++item;
                }
            }

            Init_Object(OUT, context);
        }
        else
            fail (Error_Cannot_Reflect(VAL_TYPE(value), arg));
        break;}

    default:
        fail (Error_Illegal_Action(REB_DATATYPE, verb));
    }

    return OUT;
}

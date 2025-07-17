//
//  file: %t-datatype.c
//  summary: "datatype datatype"
//  section: datatypes
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

#include "sys-core.h"


//
//  CT_Datatype: C
//
REBINT CT_Datatype(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0) return (CELL_DATATYPE_TYPE(a) == CELL_DATATYPE_TYPE(b));
    return -1;
}


//
//  MAKE_Datatype: C
//
Bounce MAKE_Datatype(Value* out, enum Reb_Kind kind, const Value* arg) {
    if (Is_Word(arg)) {
        Option(SymId) sym = Word_Id(arg);
        if (not sym or (unwrap sym >= SYM_FROM_KIND(TYPE_MAX)))
            goto bad_make;

        return Init_Datatype(out, KIND_FROM_SYM(unwrap sym));
    }

  bad_make:;
    panic (Error_Bad_Make(kind, arg));
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
void MF_Datatype(Molder* mo, const Cell* v, bool form)
{
    Symbol* name = Canon_From_Id(VAL_TYPE_SYM(v));
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
    enum Reb_Kind kind = CELL_DATATYPE_TYPE(value);

    switch (maybe Word_Id(verb)) {

    case SYM_REFLECT: {
        Option(SymId) sym = Word_Id(arg);
        if (sym == SYM_SPEC) {
            //
            // The "type specs" were loaded as an array, but this reflector
            // wants to give back an object.  Combine the array with the
            // standard object that mirrors its field order.
            //
            VarList* context = Copy_Context_Shallow_Managed(
                Cell_Varlist(Get_System(SYS_STANDARD, STD_TYPE_SPEC))
            );

            assert(CTX_TYPE(context) == TYPE_OBJECT);

            Value* var = Varlist_Slots_Head(context);
            Value* key = Varlist_Keys_Head(context);

            // !!! Account for the "invisible" self key in the current
            // stop-gap implementation of self, still default on MAKE OBJECT!s
            //
            assert(Key_Id(key) == SYM_SELF);
            ++key; ++var;

            Cell* item = Array_Head(
                CELL_DATATYPE_SPEC(Varlist_Slot(Lib_Context, SYM_FROM_KIND(kind)))
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
            panic (Error_Cannot_Reflect(Type_Of(value), arg));
        break;}

    default:
        panic (Error_Illegal_Action(TYPE_DATATYPE, verb));
    }

    return OUT;
}

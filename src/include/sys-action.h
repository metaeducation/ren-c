//
//  File: %sys-action.h
//  Summary: {action! defs AFTER %tmp-internals.h (see: %sys-rebact.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// Using a technique strongly parallel to contexts, an action is identified
// by a series which is also its paramlist, in which the 0th element is an
// archetypal value of that ACTION!.  Unlike contexts, an action does not
// have values of its own...only parameter definitions (or "params").  The
// arguments ("args") come from finding an action's instantiation on the
// stack, and can be viewed as a context using a FRAME!.
//


//=//// PSEUDOTYPES FOR RETURN VALUES /////////////////////////////////////=//
//
// An arbitrary cell pointer may be returned from a native--in which case it
// will be checked to see if it is thrown and processed if it is, or checked
// to see if it's an unmanaged API handle and released if it is...ultimately
// putting the cell into L->out.
//
// However, pseudotypes can be used to indicate special instructions to the
// evaluator.
//

// This signals that the evaluator is in a "thrown state".
//
#define BOUNCE_THROWN \
    cast(Value*, &PG_Bounce_Thrown)

// This used by path dispatch when it has taken performing a SET-PATH!
// into its own hands, but doesn't want to bother saying to move the value
// into the output slot...instead leaving that to the evaluator (as a
// SET-PATH! should always evaluate to what was just set)
//
#define BOUNCE_INVISIBLE \
    cast(Value*, &PG_Bounce_Invisible)

// If Eval_Core gets back an TYPE_R_REDO from a dispatcher, it will re-execute
// the L->phase in the frame.  This function may be changed by the dispatcher
// from what was originally called.
//
// If CELL_FLAG_FALSEY is not set on the cell, then the types will be checked
// again.  Note it is not safe to let arbitrary user code change values in a
// frame from expected types, and then let those reach an underlying native
// who thought the types had been checked.
//
#define BOUNCE_REDO_UNCHECKED \
    cast(Value*, &PG_Bounce_Redo_Unchecked)

#define BOUNCE_REDO_CHECKED \
    cast(Value*, &PG_Bounce_Redo_Checked)


// Path dispatch used to have a return value PE_SET_IF_END which meant that
// the dispatcher itself should realize whether it was doing a path get or
// set, and if it were doing a set then to write the value to set into the
// target cell.  That means it had to keep track of a pointer to a cell vs.
// putting the bits of the cell into the output.  This is now done with a
// special TYPE_R_REFERENCE type which holds in its payload a Cell and a
// specifier, which is enough to be able to do either a read or a write,
// depending on the need.
//
// !!! See notes in %c-path.c of why the R3-Alpha path dispatch is hairier
// than that.  It hasn't been addressed much in Ren-C yet, but needs a more
// generalized design.
//
#define BOUNCE_REFERENCE \
    cast(Value*, &PG_Bounce_Reference)

// This is used in path dispatch, signifying that a SET-PATH! assignment
// resulted in the updating of an immediate expression in pvs->out, meaning
// it will have to be copied back into whatever reference cell it had been in.
//
#define BOUNCE_IMMEDIATE \
    cast(Value*, &PG_Bounce_Immediate)

#define BOUNCE_UNHANDLED \
    cast(Value*, &PG_End_Node)  // ...an old and sort of superfluous conflation


INLINE Array* ACT_PARAMLIST(REBACT *a) {
    assert(Get_Array_Flag(a, IS_PARAMLIST));
    return cast(Array*, a);
}

#define ACT_ARCHETYPE(a) \
    cast(Value*, cast(Flex*, ACT_PARAMLIST(a))->content.dynamic.data)

// Functions hold their flags in their canon value, some of which are cached
// flags put there during Make_Action().
//
// !!! Review if (and how) a HIJACK might affect these flags (?)
//
#define GET_ACT_FLAG(a, flag) \
    Get_Cell_Flag(ACT_ARCHETYPE(a), flag)

#define ACT_DISPATCHER(a) \
    (MISC(ACT_ARCHETYPE(a)->payload.action.details).dispatcher)

#define ACT_DETAILS(a) \
    ACT_ARCHETYPE(a)->payload.action.details

// These are indices into the details array agreed upon by actions which have
// the CELL_FLAG_ACTION_NATIVE set.
//
#define IDX_NATIVE_BODY 0 // text string source code of native (for SOURCE)
#define IDX_NATIVE_CONTEXT 1 // libRebol binds strings here (and lib)
#define IDX_NATIVE_MAX (IDX_NATIVE_CONTEXT + 1)

INLINE Value* ACT_PARAM(REBACT *a, REBLEN n) {
    assert(n != 0 and n < Array_Len(ACT_PARAMLIST(a)));
    return Flex_At(Value, ACT_PARAMLIST(a), n);
}

#define ACT_NUM_PARAMS(a) \
    (cast(Flex*, ACT_PARAMLIST(a))->content.dynamic.len - 1)

#define ACT_META(a) \
    MISC(a).meta



// The concept of the "underlying" function is the one which has the actual
// correct paramlist identity to use for binding in adaptations.
//
// e.g. if you adapt an adaptation of a function, the keylist referred to in
// the frame has to be the one for the inner function.  Using the adaptation's
// parameter list would write variables the adapted code wouldn't read.
//
#define ACT_UNDERLYING(a) \
    (LINK(a).underlying)


// An efficiency trick makes functions that do not have exemplars NOT store
// nullptr in the LINK(info).specialty node in that case--instead the params.
// This makes Push_Action() slightly faster in assigning L->special.
//
INLINE VarList* ACT_EXEMPLAR(REBACT *a) {
    Array* details = ACT_ARCHETYPE(a)->payload.action.details;
    Array* specialty = LINK(details).specialty;
    if (Get_Array_Flag(specialty, IS_VARLIST))
        return CTX(specialty);

    return nullptr;
}

INLINE Value* ACT_SPECIALTY_HEAD(REBACT *a) {
    Array* details = ACT_ARCHETYPE(a)->payload.action.details;
    Array* s = LINK(details).specialty;
    return cast(Value*, s->content.dynamic.data) + 1; // skip archetype/root
}


// There is no binding information in a function parameter (typeset) so a
// Value* should be okay.
//
#define ACT_PARAMS_HEAD(a) \
    (cast(Value*, ACT_PARAMLIST(a)->content.dynamic.data) + 1)



//=////////////////////////////////////////////////////////////////////////=//
//
//  ACTION!
//
//=////////////////////////////////////////////////////////////////////////=//

// RETURN in the last paramlist slot
//
#define CELL_FLAG_ACTION_RETURN FLAG_TYPE_SPECIFIC_BIT(0)

#define CELL_FLAG_ACTION_UNUSED_1 FLAG_TYPE_SPECIFIC_BIT(1)

// DEFERS_LOOKBACK_ARG flag is a cached property, which tells you whether a
// function defers its first real argument when used as a lookback.  Because
// lookback dispatches cannot use refinements at this time, the answer is
// static for invocation via a plain word.  This property is calculated at
// the time of Make_Action().
//
#define CELL_FLAG_ACTION_DEFERS_LOOKBACK FLAG_TYPE_SPECIFIC_BIT(2)

// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define CELL_FLAG_ACTION_QUOTES_FIRST_ARG FLAG_TYPE_SPECIFIC_BIT(3)

// Native functions are flagged that their dispatcher represents a native in
// order to say that their ACT_DETAILS() follow the protocol that the [0]
// slot is "equivalent source" (may be a TEXT!, as in user natives, or a
// BLOCK!).  The [1] slot is a module or other context into which APIs like
// rebValue() etc. should consider for binding, in addition to lib.  A BLANK!
// in the 1 slot means no additional consideration...bind to lib only.
//
#define CELL_FLAG_ACTION_NATIVE FLAG_TYPE_SPECIFIC_BIT(4)

#define CELL_FLAG_ACTION_UNUSED_5 FLAG_TYPE_SPECIFIC_BIT(5)

// This flag is set when the native (e.g. extensions) can be unloaded
//
#define CELL_FLAG_ACTION_UNUSED_6 FLAG_TYPE_SPECIFIC_BIT(6)

#define CELL_FLAG_ACTION_7 FLAG_TYPE_SPECIFIC_BIT(7)

// ^--- !!! STOP AT FLAG_TYPE_SPECIFIC_BIT(7) !!! ---^

// These are the flags which are scanned for and set during Make_Action
//
INLINE void Clear_Action_Cached_Flags(Cell *v) {
    Clear_Cell_Flag(v, ACTION_DEFERS_LOOKBACK);
    Clear_Cell_Flag(v, ACTION_QUOTES_FIRST_ARG);
}


INLINE REBACT *VAL_ACTION(const Cell* v) {
    assert(Is_Action(v));
    Flex* s = v->payload.action.paramlist;
    if (Get_Flex_Info(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return ACT(s);
}

#define VAL_ACT_PARAMLIST(v) \
    ACT_PARAMLIST(VAL_ACTION(v))

#define VAL_ACT_NUM_PARAMS(v) \
    ACT_NUM_PARAMS(VAL_ACTION(v))

#define VAL_ACT_PARAMS_HEAD(v) \
    ACT_PARAMS_HEAD(VAL_ACTION(v))

#define VAL_ACT_PARAM(v,n) \
    ACT_PARAM(VAL_ACTION(v), n)

INLINE Array* VAL_ACT_DETAILS(const Cell* v) {
    assert(Is_Action(v));
    return v->payload.action.details;
}

INLINE Dispatcher* VAL_ACT_DISPATCHER(const Cell* v) {
    assert(Is_Action(v));
    return MISC(v->payload.action.details).dispatcher;
}

INLINE VarList* VAL_ACT_META(const Cell* v) {
    assert(Is_Action(v));
    return MISC(v->payload.action.paramlist).meta;
}


// Native values are stored in an array at boot time.  These are convenience
// routines for accessing them, which should compile to be as efficient as
// fetching any global pointer.
//
// Note: In Modern Ren-C, this is simply LIB(name)... there's no separate
// list beyond the values in the lib context, and they are looked up in O(1)
// time because the stubs are globally allocated in an array ordered by SymId.

#define NAT_VALUE(name) \
    (&Natives[N_##name##_ID])

#define NAT_ACTION(name) \
    VAL_ACTION(&Natives[N_##name##_ID])


// A fully constructed action can reconstitute the ACTION! cell
// that is its canon form from a single pointer...the cell sitting in
// the 0 slot of the action's paramlist.
//
INLINE Value* Init_Action_Unbound(
    Cell* out,
    REBACT *a
){
  #if RUNTIME_CHECKS
    Extra_Init_Action_Checks_Debug(a);
  #endif
    Force_Flex_Managed(ACT_PARAMLIST(a));
    Copy_Cell(out, ACT_ARCHETYPE(a));
    assert(VAL_BINDING(out) == UNBOUND);
    return KNOWN(out);
}

INLINE Value* Init_Action_Maybe_Bound(
    Cell* out,
    REBACT *a,
    Stub* binding // allowed to be UNBOUND
){
  #if RUNTIME_CHECKS
    Extra_Init_Action_Checks_Debug(a);
  #endif
    Force_Flex_Managed(ACT_PARAMLIST(a));
    Copy_Cell(out, ACT_ARCHETYPE(a));
    assert(VAL_BINDING(out) == UNBOUND);
    INIT_BINDING(out, binding);
    return KNOWN(out);
}

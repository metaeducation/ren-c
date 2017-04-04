//
//  File: %m-stack.c
//  Summary: "data and function call stack implementation"
//  Section: memory
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
//  Startup_Stacks: C
//
void Startup_Stacks(REBCNT size)
{
    // We always keep one chunker around for the first chunk push, and prep
    // one chunk so that the push and drop routines never worry about testing
    // for the empty case.

    TG_Root_Chunker = cast(
        struct Reb_Chunker*,
        Alloc_Mem(BASE_CHUNKER_SIZE + CS_CHUNKER_PAYLOAD)
    );

#if !defined(NDEBUG)
    memset(TG_Root_Chunker, 0xBD, sizeof(struct Reb_Chunker));
#endif

    TG_Root_Chunker->next = NULL;
    TG_Root_Chunker->size = CS_CHUNKER_PAYLOAD;
    TG_Top_Chunk = cast(struct Reb_Chunk*, &TG_Root_Chunker->payload);
    TG_Top_Chunk->prev = NULL;

    // Zero values for initial chunk, also sets offset to 0
    //
    Init_Endlike_Header(&TG_Top_Chunk->header, 0);
    TG_Top_Chunk->offset = 0;
    TG_Top_Chunk->size = BASE_CHUNK_SIZE;

    // Implicit termination trick, see notes on NODE_FLAG_END
    //
    Init_Endlike_Header(
        &cast(
            struct Reb_Chunk*, cast(REBYTE*, TG_Top_Chunk) + BASE_CHUNK_SIZE
        )->header,
        0
    );
    assert(IS_END(&TG_Top_Chunk->values[0]));

    // Start the data stack out with just one element in it, and make it an
    // unreadable blank in the debug build.  This helps avoid accidental
    // reads and is easy to notice when it is overwritten.  It also means
    // that indices into the data stack can be unsigned (no need for -1 to
    // mean empty, because 0 can)
    //
    // DS_PUSH checks what you're pushing isn't void, as most arrays can't
    // contain them.  But DS_PUSH_MAYBE_VOID allows you to, in case you
    // are building a context varlist or similar.
    //
    DS_Array = Make_Array_Core(1, ARRAY_FLAG_VOIDS_LEGAL);
    Init_Unreadable_Blank(ARR_HEAD(DS_Array));

    // The END marker will signal DS_PUSH that it has run out of space,
    // and it will perform the allocation at that time.
    //
    TERM_ARRAY_LEN(DS_Array, 1);
    ASSERT_ARRAY(DS_Array);

    // Reuse the expansion logic that happens on a DS_PUSH to get the
    // initial stack size.  It requires you to be on an END to run.
    //
    DS_Index = 1;
    DS_Movable_Base = KNOWN(ARR_HEAD(DS_Array)); // can't push RELVALs
    Expand_Data_Stack_May_Fail(size);

    // Now drop the hypothetical thing pushed that triggered the expand.
    //
    DS_DROP;

    // Call stack (includes pending functions, parens...anything that sets
    // up a `REBFRM` and calls Do_Core())  Singly linked.
    //
    TG_Frame_Stack = NULL;
}


//
//  Shutdown_Stacks: C
//
void Shutdown_Stacks(void)
{
    assert(FS_TOP == NULL);
    assert(DSP == 0);
    assert(IS_UNREADABLE_IF_DEBUG(ARR_HEAD(DS_Array)));

    Free_Array(DS_Array);

    assert(TG_Top_Chunk == cast(struct Reb_Chunk*, &TG_Root_Chunker->payload));

    // Because we always keep one chunker of headroom allocated, and the
    // push/drop is not designed to manage the last chunk, we *might* have
    // that next chunk of headroom still allocated.
    //
    if (TG_Root_Chunker->next)
        Free_Mem(TG_Root_Chunker->next, TG_Root_Chunker->next->size + BASE_CHUNKER_SIZE);

    // OTOH we always have to free the root chunker.
    //
    Free_Mem(TG_Root_Chunker, TG_Root_Chunker->size + BASE_CHUNKER_SIZE);
}


//
//  Expand_Data_Stack_May_Fail: C
//
// The data stack maintains an invariant that you may never push an END to it.
// So each push looks to see if it's pushing to a cell that contains an END
// and if so requests an expansion.
//
// WARNING: This will invalidate any extant pointers to REBVALs living in
// the stack.  It is for this reason that stack access should be done by
// REBDSP "data stack pointers" and not by REBVAL* across *any* operation
// which could do a push or pop.  (Currently stable w.r.t. pop but there may
// be compaction at some point.)
//
void Expand_Data_Stack_May_Fail(REBCNT amount)
{
    REBCNT len_old = ARR_LEN(DS_Array);

    // The current requests for expansion should only happen when the stack
    // is at its end.  Sanity check that.
    //
#if !defined(NDEBUG)
    REBVAL *end_top = DS_AT(DSP); // DS_TOP would assert on END
    assert(IS_END(end_top));
    assert(end_top == KNOWN(ARR_TAIL(DS_Array))); // can't push RELVALs
    assert(end_top - KNOWN(ARR_HEAD(DS_Array)) == cast(int, len_old));
#endif

    // If adding in the requested amount would overflow the stack limit, then
    // give a data stack overflow error.
    //
    if (SER_REST(AS_SERIES(DS_Array)) + amount >= STACK_LIMIT)
        Trap_Stack_Overflow();

    Extend_Series(AS_SERIES(DS_Array), amount);

    // Update the global pointer representing the base of the stack that
    // likely was moved by the above allocation.  (It's not necessarily a
    // huge win to cache it, but it turns data stack access from a double
    // dereference into a single dereference in the common case, and it was
    // how R3-Alpha did it).
    //
    DS_Movable_Base = KNOWN(ARR_HEAD(DS_Array)); // must do before using DS_TOP

    // We fill in the data stack with "GC safe trash" (which is void in the
    // release build, but will raise an alarm if VAL_TYPE() called on it in
    // the debug build).  In order to serve as a marker for the stack slot
    // being available, it merely must not be IS_END()...
    //
    REBVAL *value = DS_AT(DSP); // DS_TOP would assert on END
    REBCNT len_new = len_old + amount;
    REBCNT n;
    for (n = len_old; n < len_new; ++n) {
        Init_Unreadable_Blank(value);
        ++value;
    }

    // Update the end marker to serve as the indicator for when the next
    // stack push would need to expand.
    //
    TERM_ARRAY_LEN(DS_Array, len_new);
    assert(value == ARR_TAIL(DS_Array));

    ASSERT_ARRAY(DS_Array);
}


//
//  Pop_Stack_Values: C
//
// Pops computed values from the stack to make a new ARRAY.
//
REBARR *Pop_Stack_Values_Core(REBDSP dsp_start, REBUPT flags)
{
    REBARR *array = Copy_Values_Len_Shallow_Core(
        DS_AT(dsp_start + 1), // start somewhere in the stack, end at DS_TOP
        SPECIFIED, // data stack should be fully specified--no relative values
        DSP - dsp_start, // len
        flags
    );

    DS_DROP_TO(dsp_start);
    return array;
}


//
//  Pop_Stack_Values_Reversed: C
//
// Pops computed values from the stack to make a new ARRAY, but reverses the
// data so the last pushed item is the first in the array.
//
REBARR *Pop_Stack_Values_Reversed(REBDSP dsp_start)
{
    REBARR *array = Copy_Values_Len_Reversed_Shallow(
        DS_TOP, // start at DS_TOP, work backwards somewhere in the stack
        SPECIFIED, // data stack should be fully specified--no relative values
        DSP - dsp_start // len
    );

    DS_DROP_TO(dsp_start);
    return array;
}


//
//  Pop_Stack_Values_Into: C
//
// Pops computed values from the stack into an existing ANY-ARRAY.  The
// index of that array will be updated to the insertion tail (/INTO protocol)
//
void Pop_Stack_Values_Into(REBVAL *into, REBDSP dsp_start) {
    REBCNT len = DSP - dsp_start;
    REBVAL *values = KNOWN(ARR_AT(DS_Array, dsp_start + 1));

    assert(ANY_ARRAY(into));
    FAIL_IF_READ_ONLY_ARRAY(VAL_ARRAY(into));

    VAL_INDEX(into) = Insert_Series(
        AS_SERIES(VAL_ARRAY(into)),
        VAL_INDEX(into),
        cast(REBYTE*, values), // stack only holds fully specified REBVALs
        len // multiplied by width (sizeof(REBVAL)) in Insert_Series
    );

    DS_DROP_TO(dsp_start);
}


//
//  Reify_Frame_Context_Maybe_Fulfilling: C
//
// A Reb_Frame does not allocate a REBSER for its frame to be used in the
// context by default.  But one can be allocated on demand, even for a NATIVE!
// in order to have a binding location for the debugger (for instance).
// If it becomes necessary to create words bound into the frame that is
// another case where the frame needs to be brought into existence.
//
// If there's already a frame this will return it, otherwise create it.
//
void Reify_Frame_Context_Maybe_Fulfilling(REBFRM *f) {
    assert(Is_Any_Function_Frame(f)); // varargs reifies while still pending

    if (f->varlist != NULL) {
        //
        // We have our function call's args in an array, but it is not yet
        // a context.  !!! Really this cannot reify if we're in arg gathering
        // mode, calling MANAGE_ARRAY is illegal -- need test for that !!!
        //
        assert(Not_Ser_Flag(f->varlist, ARRAY_FLAG_VARLIST));
        Set_Ser_Flag(f->varlist, ARRAY_FLAG_VARLIST);

        assert(IS_TRASH_DEBUG(ARR_AT(f->varlist, 0))); // we fill this in
        assert(Get_Ser_Info(f->varlist, SERIES_INFO_HAS_DYNAMIC));
    }
    else {
        f->varlist = Alloc_Singular_Array_Core(
            ARRAY_FLAG_VARLIST | CONTEXT_FLAG_STACK
        );
    }

    REBCTX *c = AS_CONTEXT(f->varlist);

    // We do not Manage_Context, because we are reusing a word series here
    // that has already been managed.  The arglist array was managed when
    // created and kept alive by Mark_Call_Frames
    //
    INIT_CTX_KEYLIST_SHARED(c, FUNC_PARAMLIST(FRM_UNDERLYING(f)));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(c));

    // When in ET_FUNCTION or ET_LOOKBACK, the arglist will be marked safe from
    // GC. It is managed because the pointer makes its way into bindings that
    // ANY-WORD! values may have, and they need to not crash.
    //
    // !!! Note that theoretically pending mode arrays do not need GC
    // access as no running code could get them, but the debugger is
    // able to access this information.  This is under review for how it
    // might be stopped.
    //
    REBVAL *rootvar = SINK(ARR_HEAD(f->varlist));
    Reset_Val_Header(rootvar, REB_FRAME);
    rootvar->payload.any_context.varlist = f->varlist;
    rootvar->payload.any_context.phase = f->phase;
    rootvar->extra.binding = f->binding;

    AS_SERIES(f->varlist)->misc.f = f;

    // A reification of a frame for native code should not allow changing
    // the values out from under it, because that could cause it to crash
    // the interpreter.  (Generally speaking, modification should only be
    // possible in the debugger anyway.)  For now, mark the array as
    // running...which should not stop FRM_ARG from working in the native
    // itself, but should stop modifications from user code.
    //
    if (f->flags.bits & DO_FLAG_NATIVE_HOLD)
        Set_Ser_Info(f->varlist, SERIES_INFO_RUNNING);

    MANAGE_ARRAY(f->varlist);

#if !defined(NDEBUG)
    //
    // Variadics will reify the varlist even when the data is not quite
    // ready; these need special handling in the GC code for marking frames.
    // By the time the function actually runs, the data should be good.
    //
    if (NOT(Is_Function_Frame_Fulfilling(f)))
        ASSERT_CONTEXT(c);
    assert(NOT(CTX_VARS_UNAVAILABLE(c)));
#endif
}

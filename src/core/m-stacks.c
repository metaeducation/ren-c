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
**  Module:  m-stack.c
**  Summary: data and function call stack implementation
**  Section: memory
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
//  Init_Stacks: C
//
void Init_Stacks(REBCNT size)
{
    // We always keep one call stack chunk frame around for the first
    // call frame push.  The first frame allocated out of it is
    // saved as CS_Root.

    struct Reb_Chunk *chunk = ALLOC(struct Reb_Chunk);
#if !defined(NDEBUG)
    memset(chunk, 0xBD, sizeof(struct Reb_Chunk));
#endif
    chunk->next = NULL;
    CS_Root = cast(struct Reb_Call*, &chunk->payload);

    CS_Top = NULL;
    CS_Running = NULL;

    DS_Series = Make_Array(size);
    Set_Root_Series(TASK_STACK, DS_Series, "data stack"); // uses special GC
}


//
//  Shutdown_Stacks: C
//
void Shutdown_Stacks(void)
{
    struct Reb_Chunk* chunk = cast(struct Reb_Chunk*,
        cast(REBYTE*, CS_Root) - sizeof(struct Reb_Chunk*)
    );

    assert(!CS_Running);
    assert(!CS_Top);

    FREE(struct Reb_Chunk, chunk);

    assert(DSP == -1);
}


//
//  Push_Stack_Values: C
// 
// Pushes sequential values from a series onto the stack all
// in one go.  All of this needs review in terms of whether
// things like COMPOSE should be using arbitrary stack pushes
//      in the first place or if it should not pile up the stack
// like this.
// 
// !!! Notably simple implementation, just hammering out the
// client interfaces that made sequential stack memory assumptions.
//
void Push_Stack_Values(const REBVAL *values, REBINT length)
{
    Insert_Series(
        DS_Series, SERIES_TAIL(DS_Series), cast(const REBYTE*, values), length
    );
}


//
//  Pop_Stack_Values: C
// 
// Pop_Stack_Values computed values from the stack into the series
// specified by "into", or if into is NULL then store it as a
// block on top of the stack.  (Also checks to see if into
// is protected, and will trigger a trap if that is the case.)
// 
// Protocol for /INTO is to set the position to the tail.
//
void Pop_Stack_Values(REBVAL *out, REBINT dsp_start, REBOOL into)
{
    REBSER *series;
    REBCNT len = DSP - dsp_start;
    REBVAL *values = BLK_SKIP(DS_Series, dsp_start + 1);

    if (into) {
        assert(ANY_ARRAY(out));
        series = VAL_SERIES(out);
        if (IS_PROTECT_SERIES(series)) fail (Error(RE_PROTECTED));
        VAL_INDEX(out) = Insert_Series(
            series, VAL_INDEX(out), cast(REBYTE*, values), len
        );
    }
    else {
        series = Copy_Values_Len_Shallow(values, len);
        Val_Init_Block(out, series);
    }

    DS_DROP_TO(dsp_start);
}


//
//  Expand_Stack: C
// 
// Expand the datastack. Invalidates any references to stack
// values, so code should generally use stack index integers,
// not pointers into the stack.
//
void Expand_Stack(REBCNT amount)
{
    if (SERIES_REST(DS_Series) >= STACK_LIMIT) Trap_Stack_Overflow();
    Extend_Series(DS_Series, amount);
    Debug_Fmt(cs_cast(BOOT_STR(RS_STACK, 0)), DSP, SERIES_REST(DS_Series));
}


//
//  Make_Call: C
// 
// Create a function call frame.  This doesn't necessarily call
// Alloc_Mem, because call frames are allocated sequentially
// inside of "memory chunks" in their ordering on the stack.
// Allocation is only required if we need to step into a new
// chunk (and even then only if we aren't stepping into a chunk
// that we are reusing from a prior expansion).
// 
// We do not set the frame as "Running" at the same time we create
// it, because we need to fulfill its arguments in the caller's
// frame before we actually invoke the function.
//
struct Reb_Call *Make_Call(
    REBVAL *out,
    REBSER *block,
    REBCNT index,
    const REBVAL *label,
    const REBVAL *func
) {
    REBCNT num_vars = VAL_FUNC_NUM_PARAMS(func);

    REBCNT size = (
        sizeof(struct Reb_Call)
        + sizeof(REBVAL) * (num_vars > 0 ? num_vars - 1 : 0)
    );

    struct Reb_Call *call;

    // Establish invariant where 'call' points to a location big enough to
    // hold the new frame (with frame's size accounted for in chunk_size)

    if (!CS_Top) {
        // If not big enough, a new chunk wouldn't be big enough, either!
        assert(size <= CS_CHUNK_PAYLOAD);

        // Claim the root frame
        call = CS_Root;
        call->chunk_left = CS_CHUNK_PAYLOAD - size;
    }
    else if (CS_Top->chunk_left >= cast(REBINT, size)) { // Chunk has space

        // Advance past the topmost frame (whose size depends on num_vars)
        call = cast(struct Reb_Call*,
            cast(REBYTE*, CS_Top) + DSF_SIZE(CS_Top)
        );

        // top's chunk_left accounted for previous frame, account for ours
        call->chunk_left = CS_Top->chunk_left - size;
    }
    else { // Not enough space

        struct Reb_Chunk *chunk = DSF_CHUNK(CS_Top);

        if (chunk->next) {
            // Previously allocated chunk exists already to grow into
            assert(!chunk->next->next);
        }
        else {
            // No previously allocated chunk...we have to allocate it
            chunk->next = ALLOC(struct Reb_Chunk);
            chunk->next->next = NULL;
        }

        call = cast(struct Reb_Call*, &chunk->next->payload);
        call->chunk_left = CS_CHUNK_PAYLOAD - size;
    }

    assert(call->chunk_left >= 0);

    // Even though we can't push this stack frame to the CSP yet, it
    // still needs to be considered for GC and freeing in case of a
    // trap.  In a recursive DO we can get many pending frames before
    // we come back to actually putting the topmost one in effect.
    // !!! Better design for call frame stack coming.

    call->prior = CS_Top;
    CS_Top = call;

    call->args_ready = FALSE;

    call->out = out;

    assert(ANY_FUNC(func));
    call->func = *func;

    assert(block); // Don't accept NULL series
    Val_Init_Block_Index(&call->where, block, index);

    // Save symbol describing the function (if we called this as the result of
    // a word or path lookup)
    if (!label) {
        // !!! When a function was not invoked through looking up a word to
        // (or a word in a path) to use as a label, there were three different
        // alternate labels used.  One was SYM__APPLY_, another was
        // ROOT_NONAME, and another was to be the type of the function being
        // executed.  None are fantastic, but we do the type for now.
        Val_Init_Word_Unbound(
            &call->label, REB_WORD, SYM_FROM_KIND(VAL_TYPE(func))
        );
    }
    else
        call->label = *label;

    assert(IS_WORD(DSF_LABEL(call)));

    call->num_vars = num_vars;

    // Make_Call does not fill the args in the frame--that is up to Do_Core
    // and Apply_Block to do as they go along.  But the frame has to survive
    // Recycle() during arg fulfillment...slots can't be left uninitialized.
    // It is important to set to UNSET for bookkeeping so that refinement
    // scanning knows when it has filled a refinement or not.
    {
        REBCNT index;
        for (index = 0; index < num_vars; index++)
            SET_UNSET(&call->vars[index]);
    }

    assert(size == DSF_SIZE(call));

    return call;
}


//
//  Free_Call: C
// 
// Free a call frame.  This only occasionally requires an actual
// call to Free_Mem(), due to allocating call frames sequentially
// in chunks of memory.
//
void Free_Call(struct Reb_Call* call)
{
    assert(call == CS_Top);

    // Drop to the prior top call stack frame
    CS_Top = call->prior;

    if (cast(REBCNT, call->chunk_left) == CS_CHUNK_PAYLOAD - DSF_SIZE(call)) {
        // This call frame sits at the head of a chunk.

        struct Reb_Chunk *chunk = cast(struct Reb_Chunk *,
            cast(REBYTE*, call) - sizeof(struct Reb_Chunk*)
        );
        assert(DSF_CHUNK(call) == chunk);

        // When we've completely emptied a chunk, we check to see if the
        // chunk after it is still live.  If so, we free it.  But we
        // want to keep *this* just-emptied chunk alive for overflows if we
        // rapidly get another push, to avoid Make_Mem()/Free_Mem() costs.

        if (chunk->next) {
            FREE(struct Reb_Chunk, chunk->next);
            chunk->next = NULL;
        }
    }

    // In debug builds we poison the memory for the frame
#if !defined(NDEBUG)
    memset(call, 0xBD, DSF_SIZE(call));
#endif
}


#if !defined(NDEBUG)

//
//  DSF_VAR_Debug: C
// 
// Debug-only version of getting a variable out of a call
// frame, which asserts if you use an index that is higher
// than the number of arguments in the frame.
//
REBVAL *DSF_VAR_Debug(struct Reb_Call *call, REBCNT n)
{
    assert(n <= call->num_vars);
    return &call->vars[n - 1];
}

#endif

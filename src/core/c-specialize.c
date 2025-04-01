//
//  File: %c-specialize.c
//  Summary: "function related datatypes"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2018 Rebol Open Source Contributors
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
// A specialization is an ACTION! which has some of its parameters fixed.
// e.g. `ap10: specialize 'append [value: 5 + 5]` makes ap10 have all the same
// refinements available as APPEND, but otherwise just takes one series arg,
// as it will always be appending 10.
//
// The method used is to store a FRAME! in the specialization's Action Body.
// It contains non-null values for any arguments that have been specialized.
// Eval_Core_Throws() heeds these when walking parameters (see `L->special`),
// and processes slots with nulls in them normally.
//
// Code is shared between the SPECIALIZE native and specialization of a
// GET-PATH! via refinements, such as `adp: :append/dup/part`.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The original design of the specialize mechanic attempted to do some very
//    complicated things with partial specialization.  This was aggravated by
//    the fact that refinement signals were separate from the arguments that
//    they corresponded to...and synchronizing that was a pain.  Refinements
//    could also take multiple arguments.  Modern Ren-C has simplified that:
//
//    https://forum.rebol.info/t/simplifying-refinements-to-1-or-0-args/1120
//
//    Hence this buggy codebase has been pared down such that it doesn't try
//    to do anything that modern Ren-C can't.  Asking for conflicting behavior
//    (like setting a refinement to true, while its argument is null) will
//    produce arbitrary (but hopefully non-crashing) behavior.
//


#include "sys-core.h"


//
//  Cleanup_Specialization_Binder: C
//
// Binders are strange, in that they're implemented by storing information
// in places that are slow to enumerate.  So you have to apply whatever you
// used to make the binder in reverse to remove the information.
//
void Cleanup_Specialization_Binder(
    struct Reb_Binder* binder,
    VarList* exemplar
){
    Value* key = Varlist_Keys_Head(exemplar);
    Value* var = Varlist_Slots_Head(exemplar);
    for (; NOT_END(key); ++key, ++var) {
        if (Is_Param_Unbindable(key))
            continue; // !!! is this flag still relevant?
        if (Is_Param_Hidden(key)) {
            assert(Get_Cell_Flag(var, ARG_MARKED_CHECKED));  // ??
            continue; // was part of a specialization internal to the action
        }
        if (Get_Cell_Flag(var, ARG_MARKED_CHECKED))  // ??
            continue; // may be refinement from stack, now specialized out
        Remove_Binder_Index(binder, Key_Canon(key));
    }
    SHUTDOWN_BINDER(binder);  // must do before running code that might BIND
}


//
//  Make_Managed_Context_For_Action_May_Fail: C
//
// This creates a FRAME! context with "Nulled" in all the unspecialized slots
// that are available to be filled.  It looks on the stack to find any pushed
// refinements, and if they are present they will be set in the frame if they
// take no arguments.
//
// Support is not provided for stack arguments that specify refinements that
// take parameters, e.g.
//
//     >> specialize :append/dup []
//     ** Error: SPECIALIZE does not support refinement promotion
//
//     >> specialize :append [dup: true]
//     ** Error: SPECIALIZE does not support refinement promotion
//
//     >> specialize :append [dup: true count: 5]
//     ; this is okay
//
//     >> specialize :append [count: 5]
//     ; this is also okay (automatically assumes dup as true)
//
VarList* Make_Managed_Context_For_Action_May_Fail(
    const Value* action,   // need ->binding, so can't just be a REBACT*
    StackIndex lowest_stackindex, // caller can add refinement specializations
    struct Reb_Binder *opt_binder  // must call cleanup if passed non-null
){
    if (opt_binder)
        INIT_BINDER(opt_binder, nullptr);

    StackIndex highest_stackindex = TOP_INDEX;  // highest ordered refinement

    REBACT *act = VAL_ACTION(action);

    REBLEN num_slots = ACT_NUM_PARAMS(act) + 1;
    Array* varlist = Make_Array_Core(
        num_slots, // includes +1 for the Varlist_Archetype() at [0]
        SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED
    );

    Value* rootvar = RESET_CELL(Array_Head(varlist), TYPE_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = VAL_ACTION(action);
    INIT_BINDING(rootvar, VAL_BINDING(action));

    // Copy values from any prior specializations.

    const Value* param = ACT_PARAMS_HEAD(act);
    Value* arg = rootvar + 1;
    const Value* special = ACT_SPECIALTY_HEAD(act); // of exemplar/paramlist

    REBLEN index = 1; // used to bind REFINEMENT! values to parameter slots

    VarList* exemplar = ACT_EXEMPLAR(act); // may be null
    if (exemplar)
        assert(special == Varlist_Slots_Head(exemplar));
    else
        assert(special == ACT_PARAMS_HEAD(act));

    for (; NOT_END(param); ++param, ++arg, ++special, ++index) {
        arg->header.bits = CELL_MASK_ERASE;

        Symbol* canon = Cell_Param_Canon(param);

        assert(special != param or Not_Cell_Flag(arg, ARG_MARKED_CHECKED));

    //=//// NON-REFINEMENT SLOT HANDLING //////////////////////////////////=//

        if (Cell_Parameter_Class(param) != PARAMCLASS_REFINEMENT) {
            if (Is_Param_Hidden(param)) {
                assert(Get_Cell_Flag(special, ARG_MARKED_CHECKED));
                Copy_Cell(arg, special); // !!! copy the flag?
                Set_Cell_Flag(arg, ARG_MARKED_CHECKED); // !!! not copied
                goto continue_specialized; // Eval_Core_Throws() checks type
            }
            goto continue_unspecialized;
        }

    //=//// REFINEMENT PARAMETER HANDLING /////////////////////////////////=//

        if (Is_Blank(special)) { // specialized BLANK! => "disabled"
            Init_Blank(arg);
            Set_Cell_Flag(arg, ARG_MARKED_CHECKED);
            goto continue_specialized;
        }

        if (Is_Refinement(special)) { // specialized REFINEMENT! => "in use"
            Init_Refinement(arg, Cell_Parameter_Symbol(param));
            Set_Cell_Flag(arg, ARG_MARKED_CHECKED);
            goto continue_specialized;
        }

        assert(
            special == param
            or Is_Nulled(special)
            or (
                Is_Nothing(special)
                and Get_Cell_Flag(special, ARG_MARKED_CHECKED)
            )
        );

        // If we get here, then the refinement is unspecified in the
        // exemplar (or there is no exemplar and special == param).
        // *but* the passed in refinements may wish to override that in
        // a "virtual" sense...and remove it from binding consideration
        // for a specialization, e.g.
        //
        //     specialize 'append/only [only: false] ; won't disable only
        {
            StackIndex stackindex = highest_stackindex;
            for (; stackindex != lowest_stackindex; --stackindex) {
                Value* ordered = Data_Stack_At(stackindex);
                if (VAL_STORED_CANON(ordered) != canon)
                    continue; // just continuing this loop

                assert(not IS_WORD_BOUND(ordered)); // we bind only one
                INIT_BINDING(ordered, varlist);
                ordered->payload.any_word.index = index;

                // Wasn't hidden in the incoming paramlist, but it should be
                // hidden from the user when they are running their code
                // bound into this frame--even before the specialization
                // based on the outcome of that code has been calculated.
                //
                Init_Refinement(arg, Cell_Parameter_Symbol(param));
                Set_Cell_Flag(arg, ARG_MARKED_CHECKED);
                goto continue_specialized;
            }
        }

        goto continue_unspecialized;

      continue_unspecialized:;

        assert(arg->header.bits == CELL_MASK_ERASE);
        Init_Nulled(arg);
        if (opt_binder) {
            if (not Is_Param_Unbindable(param))
                Add_Binder_Index(opt_binder, canon, index);
        }
        continue;

      continue_specialized:;

        assert(not Is_Nulled(arg));
        assert(Get_Cell_Flag(arg, ARG_MARKED_CHECKED));
        continue;
    }

    Term_Array_Len(varlist, num_slots);
    MISC(varlist).meta = nullptr;  // GC sees this, we must initialize

    Tweak_Keylist_Of_Varlist_Shared(CTX(varlist), ACT_PARAMLIST(act));

    while (TOP_INDEX != lowest_stackindex) {
        if (not IS_WORD_BOUND(TOP)) {
            DECLARE_VALUE (bad);
            Copy_Cell(bad, TOP);

            Drop_Data_Stack_To(lowest_stackindex);
            if (opt_binder)
                Cleanup_Specialization_Binder(opt_binder, CTX(varlist));

            fail (Error_Bad_Refine_Raw(bad));
        }

        DROP();
    }

    return CTX(varlist);
}


//
//  Specialize_Action_Throws: C
//
// Create a new ACTION! value that uses the same implementation as another,
// but just takes fewer arguments or refinements.  It does this by storing a
// heap-based "exemplar" FRAME! in the specialized action; this stores the
// values to preload in the stack frame cells when it is invoked.
//
bool Specialize_Action_Throws(
    Value* out,
    Value* specializee,
    Symbol* opt_specializee_name,
    Value* opt_def, // !!! REVIEW: binding modified directly (not copied)
    StackIndex lowest_stackindex
){
    assert(out != specializee);

    struct Reb_Binder binder;

    REBACT *unspecialized = VAL_ACTION(specializee);

  //=//// MAKE CONTEXT TO STORE SPECIALIZED ARGUMENT VALUES ///////////////=//

    // The slots in the frame will be specialized if they were specialized
    // in the original, otherwise they'll be set to null...unless there is
    // a symbol pushed on the stack (e.g. specialize :append/only [...])
    // in which case ONLY will be on the stack, and incorporated as a
    // "specialized out" value not visible to any code running in the body.

    VarList* exemplar = Make_Managed_Context_For_Action_May_Fail(
        specializee,
        lowest_stackindex,
        opt_def ? &binder : nullptr
    );

    if (not opt_def)
        goto build_paramlist_on_stack;

{ //=//// BIND PROVIDED CODE THAT FILLS FRAME (FULLY OR PARTIALLY) ////////=//

    // Bind all the SET-WORD! in the body that match params in the frame
    // into the frame.  This means `value: value` can very likely have
    // `value:` bound for assignments into the frame while `value` refers
    // to whatever value was in the context the specialization is running
    // in, but this is likely the more useful behavior.
    //
    // !!! This binds the actual arg data, not a copy of it--following
    // OBJECT!'s lead.  However, ordinary functions make a copy of the
    // body they are passed before rebinding.  Virtual binding in modern
    // Ren-C resolves all of this.

    Bind_Values_Inner_Loop(
        &binder,
        Cell_List_At(opt_def),
        exemplar,
        FLAGIT_KIND(TYPE_SET_WORD), // types to bind (just set-word!)
        0, // types to "add midstream" to binding as we go (nothing)
        BIND_DEEP
    );

    Cleanup_Specialization_Binder(&binder, exemplar);

  //=//// RUN PROVIDED CODE (IGNORE RESULT, UNLESS IT IS THROWN) //////////=//

    Push_GC_Guard(exemplar);
    bool threw = Eval_List_At_Throws(out, opt_def);
    Drop_GC_Guard(exemplar);

    if (threw) {
        Drop_Data_Stack_To(lowest_stackindex);
        return true;
    }

} build_paramlist_on_stack: { /////////////////////////////////////////////=//

    Value* rootkey = CTX_ROOTKEY(exemplar);

    StackIndex paramlist_base = TOP_INDEX;
    Copy_Cell(PUSH(), ACT_ARCHETYPE(unspecialized));

    Value* param = rootkey + 1;
    Value* arg = Varlist_Slots_Head(exemplar);
    REBLEN index = 1;

    for (; NOT_END(param); ++param, ++arg, ++index) {
        switch (Cell_Parameter_Class(param)) {
          case PARAMCLASS_REFINEMENT: {
            ParamClass pclass_next = PARAMCLASS_LOCAL;  // or END
            if (not IS_END(param + 1))
                pclass_next = Cell_Parameter_Class(param + 1);
            if (
                pclass_next != PARAMCLASS_NORMAL
                and pclass_next != PARAMCLASS_HARD_QUOTE
                and pclass_next != PARAMCLASS_SOFT_QUOTE
            ){
                // Assume refinement takes no arguments.

                if (Is_Nulled(arg))
                    goto unspecialized_arg;

                if (IS_TRUTHY(arg))
                    Init_Refinement(arg, Cell_Parameter_Symbol(param));
                else
                    Init_Blank(arg);

                Set_Cell_Flag(arg, ARG_MARKED_CHECKED);
                goto specialized_arg_no_typecheck;
            }

            if (Is_Nulled(arg + 1)) {  // takes arg, but revoke refinement
                Init_Nulled(arg);
                goto unspecialized_arg;
            }
            Init_Refinement(arg, Cell_Parameter_Symbol(param));
            Set_Cell_Flag(arg, ARG_MARKED_CHECKED);
            goto specialized_arg_no_typecheck; }

          case PARAMCLASS_RETURN:
          case PARAMCLASS_LOCAL:
            assert(Is_Nulled(arg)); // no bindings, you can't set these
            goto unspecialized_arg;

          default:
            break;
        }

        // It's an argument, either a normal one or a refinement arg.

        if (Is_Nulled(arg))
            goto unspecialized_arg;

        goto specialized_arg;

    unspecialized_arg:;

        assert(Not_Cell_Flag(arg, ARG_MARKED_CHECKED));
        Copy_Cell(PUSH(), param);  // if evoked, DROP'd from the paramlist
        continue;

    specialized_arg:;

        assert(Cell_Parameter_Class(param) != PARAMCLASS_REFINEMENT);

        // !!! If argument was previously specialized, should have been type
        // checked already... don't type check again (?)
        //
        if (Is_Param_Variadic(param))
            fail ("Cannot currently SPECIALIZE variadic arguments.");

        if (not Typeset_Check(param, Type_Of(arg)))
            fail (Error_Invalid(arg)); // !!! merge w/Error_Invalid_Arg()

       Set_Cell_Flag(arg, ARG_MARKED_CHECKED);

    specialized_arg_no_typecheck:;

        // Specialized-out arguments must still be in the parameter list,
        // for enumeration in the evaluator to line up with the frame values
        // of the underlying function.

        Copy_Cell(PUSH(), param);
        Set_Typeset_Flag(TOP, TYPE_TS_HIDDEN);
        continue;
    }

    Array* paramlist = Pop_Stack_Values_Core(
        paramlist_base,
        SERIES_MASK_ACTION
    );
    Manage_Flex(paramlist);
    Cell* rootparam = Array_Head(paramlist);
    rootparam->payload.action.paramlist = paramlist;

    // See %sysobj.r for `specialized-meta:` object template

    Value* example = Get_System(SYS_STANDARD, STD_SPECIALIZED_META);

    VarList* meta = Copy_Context_Shallow_Managed(Cell_Varlist(example));

    Init_Nulled(Varlist_Slot(meta, STD_SPECIALIZED_META_DESCRIPTION)); // default
    Copy_Cell(
        Varlist_Slot(meta, STD_SPECIALIZED_META_SPECIALIZEE),
        specializee
    );
    if (not opt_specializee_name)
        Init_Nulled(Varlist_Slot(meta, STD_SPECIALIZED_META_SPECIALIZEE_NAME));
    else
        Init_Word(
            Varlist_Slot(meta, STD_SPECIALIZED_META_SPECIALIZEE_NAME),
            opt_specializee_name
        );

    MISC(paramlist).meta = meta;

    REBACT *specialized = Make_Action(
        paramlist,
        &Specializer_Dispatcher,
        ACT_UNDERLYING(unspecialized), // same underlying action as this
        exemplar, // also provide a context of specialization values
        1 // details array capacity
    );
    assert(Keylist_Of_Varlist(exemplar) == ACT_PARAMLIST(unspecialized));

    // The "body" is the FRAME! value of the specialization.  It takes on the
    // binding we want to use (which we can't put in the exemplar archetype,
    // that binding has to be UNBOUND).  It also remembers the original
    // action in the phase, so Specializer_Dispatcher() knows what to call.
    //
    Cell* body = Array_Head(ACT_DETAILS(specialized));
    Copy_Cell(body, Varlist_Archetype(exemplar));
    INIT_BINDING(body, VAL_BINDING(specializee));
    body->payload.any_context.phase = unspecialized;

    Init_Action_Unbound(out, specialized);
    return false; // code block did not throw
}}


//
//  Specializer_Dispatcher: C
//
// The evaluator does not do any special "running" of a specialized frame.
// All of the contribution that the specialization had to make was taken care
// of when Eval_Core_Throws() used L->special to fill from the exemplar.  So
// all this does is change the phase and binding to match the function this
// layer was specializing.
//
Bounce Specializer_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));

    Value* exemplar = KNOWN(Array_Head(details));
    assert(Is_Frame(exemplar));

    Level_Phase(L) = exemplar->payload.any_context.phase;
    LVL_BINDING(L) = VAL_BINDING(exemplar);

    return BOUNCE_REDO_UNCHECKED; // redo uses the updated phase and binding
}


//
//  specialize: native [
//
//  {Create a new action through partial or full specialization of another}
//
//      return: [action!]
//      specializee [action! word! path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Definition for FRAME! fields for args and refinements}
//  ]
//
DECLARE_NATIVE(SPECIALIZE)
{
    INCLUDE_PARAMS_OF_SPECIALIZE;

    Value* specializee = ARG(SPECIALIZEE);

    StackIndex lowest_stackindex = TOP_INDEX;

    // Any partial refinement specializations are pushed to the stack, and
    // gives ordering information that TRUE assigned in a code block can't.
    //
    Symbol* opt_name;
    if (Get_If_Word_Or_Path_Throws(
        OUT,
        &opt_name,
        specializee,
        SPECIFIED,
        true // push_refines = true (don't generate temp specialization)
    )){
        // e.g. `specialize 'append/(throw 10 'dup) [value: 20]`
        //
        return BOUNCE_THROWN;
    }

    // Note: Even if there was a PATH! doesn't mean there were refinements
    // used, e.g. `specialize 'lib/append [...]`.

    if (not Is_Action(OUT))
        fail (Error_Invalid(specializee));
    Copy_Cell(specializee, OUT); // Frees OUT, and GC safe (in ARG slot)

    if (Specialize_Action_Throws(
        OUT,
        specializee,
        opt_name,
        ARG(DEF),
        lowest_stackindex
    )){
        // e.g. `specialize 'append/dup [value: throw 10]`
        //
        return BOUNCE_THROWN;
    }

    return OUT;
}


//
//  Block_Dispatcher: C
//
// There are no arguments or locals to worry about in a DOES, nor does it
// heed any definitional RETURN.  This means that in many common cases we
// don't need to do anything special to a BLOCK! passed to DO...no copying
// or otherwise.  Just run it when the function gets called.
//
// Yet `does [...]` isn't *quite* like `specialize 'eval [source: [...]]`.  The
// difference is subtle, but important when interacting with bindings to
// fields in derived objects.  That interaction cannot currently resolve such
// bindings without a copy, so it is made on demand.
//
// (Luckily these copies are often not needed, such as when the DOES is not
// used in a method... -AND- it only needs to be made once.)
//
Bounce Block_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* block = Array_Head(details);
    assert(Is_Block(block));

    if (IS_SPECIFIC(block)) {
        if (LVL_BINDING(L) == UNBOUND) {
            if (Eval_List_At_Throws(L->out, KNOWN(block)))
                return BOUNCE_THROWN;
            return L->out;
        }

        // Until "virtual binding" is implemented, we would lose f->binding's
        // ability to influence any variable lookups in the block if we did
        // not relativize it to this frame.  This is the only current way to
        // "beam down" influence of the binding for cases like:
        //
        // What forces us to copy the block are cases like this:
        //
        //     o1: make object! [a: 10 b: does [if true [a]]]
        //     o2: make o1 [a: 20]
        //     o2/b = 20
        //
        // While o2/b's ACTION! has a ->binding to o2, the only way for the
        // [a] block to get the memo is if it is relative to o2/b.  It won't
        // be relative to o2/b if it didn't have its existing relativism
        // Derelativize()'d out to make it specific, and then re-relativized
        // through a copy on behalf of o2/b.

        Array* body_array = Copy_And_Bind_Relative_Deep_Managed(
            KNOWN(block),
            ACT_PARAMLIST(Level_Phase(L)),
            TS_WORD
        );

        // Preserve file and line information from the original, if present.
        //
        if (Get_Array_Flag(Cell_Array(block), HAS_FILE_LINE)) {
            LINK(body_array).file = LINK(Cell_Array(block)).file;
            MISC(body_array).line = MISC(Cell_Array(block)).line;
            Set_Array_Flag(body_array, HAS_FILE_LINE);
        }

        // Need to do a raw initialization of this block Cell because it is
        // relative to a function.  (Init_Block assumes all specific values.)
        //
        INIT_VAL_ARRAY(block, body_array);
        VAL_INDEX(block) = 0;
        INIT_BINDING(block, Level_Phase(L)); // relative binding

        // Block is now a relativized copy; we won't do this again.
    }

    assert(IS_RELATIVE(block));

    if (Eval_Array_At_Throws(
        L->out,
        Cell_Array(block),
        VAL_INDEX(block),
        SPC(L->varlist)
    )){
        return BOUNCE_THROWN;
    }

    return L->out;
}


//
//  does: native [
//
//  {Create an arity-0 function that runs a block}
//
//      return: [action!]
//      value [block!]
//  ]
//
DECLARE_NATIVE(DOES)
{
    INCLUDE_PARAMS_OF_DOES;

    Value* value = ARG(VALUE);

    Array* paramlist = Make_Array_Core(
        1, // archetype only...DOES always makes action with no arguments
        SERIES_MASK_ACTION
    );

    Value* archetype = RESET_CELL(Alloc_Tail_Array(paramlist), TYPE_ACTION);
    archetype->payload.action.paramlist = paramlist;
    INIT_BINDING(archetype, UNBOUND);
    Term_Array_Len(paramlist, 1);

    MISC(paramlist).meta = nullptr; // REDESCRIBE can be used to add help

    //
    // `does [...]` and `does eval [...]` are not exactly the same.  The
    // generated ACTION! of the first form uses Block_Dispatcher() and
    // does on-demand relativization, so it's "kind of like" a `func []`
    // in forwarding references to members of derived objects.  Also, it
    // is optimized to not run the block with the DO native...hence a
    // HIJACK of DO won't be triggered by invocations of the first form.
    //
    Manage_Flex(paramlist);
    REBACT *doer = Make_Action(
        paramlist,
        &Block_Dispatcher, // **SEE COMMENTS**, not quite like plain DO!
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        1 // details array capacity
    );

    // Block_Dispatcher() *may* copy at an indeterminate time, so to keep
    // things invariant we have to lock it.
    //
    Cell* body = Array_Head(ACT_DETAILS(doer));
    Flex* locker = nullptr;
    Force_Value_Frozen_Deep(value, locker);
    Copy_Cell(body, value);

    return Init_Action_Unbound(OUT, doer);
}

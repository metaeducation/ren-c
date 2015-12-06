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
**  Module:  n-control.c
**  Summary: native functions for control flow
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


// Local flags used for Protect functions below:
enum {
    PROT_SET,
    PROT_DEEP,
    PROT_HIDE,
    PROT_WORD,
    PROT_MAX
};


//
//  Protect_Key: C
//
static void Protect_Key(REBVAL *key, REBCNT flags)
{
    if (GET_FLAG(flags, PROT_WORD)) {
        if (GET_FLAG(flags, PROT_SET)) VAL_SET_EXT(key, EXT_WORD_LOCK);
        else VAL_CLR_EXT(key, EXT_WORD_LOCK);
    }

    if (GET_FLAG(flags, PROT_HIDE)) {
        if GET_FLAG(flags, PROT_SET) VAL_SET_EXT(key, EXT_WORD_HIDE);
        else VAL_CLR_EXT(key, EXT_WORD_HIDE);
    }
}


//
//  Protect_Value: C
// 
// Anything that calls this must call Unmark() when done.
//
static void Protect_Value(REBVAL *value, REBCNT flags)
{
    if (ANY_SERIES(value) || IS_MAP(value))
        Protect_Series(value, flags);
    else if (IS_OBJECT(value) || IS_MODULE(value))
        Protect_Object(value, flags);
}


//
//  Protect_Series: C
// 
// Anything that calls this must call Unmark() when done.
//
void Protect_Series(REBVAL *val, REBCNT flags)
{
    REBSER *series = VAL_SERIES(val);

    if (SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

    if (GET_FLAG(flags, PROT_SET))
        SERIES_SET_FLAG(series, SER_PROTECT);
    else
        SERIES_CLR_FLAG(series, SER_PROTECT);

    if (!ANY_ARRAY(val) || !GET_FLAG(flags, PROT_DEEP)) return;

    SERIES_SET_FLAG(series, SER_MARK); // recursion protection

    for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
        Protect_Value(val, flags);
    }
}


//
//  Protect_Object: C
// 
// Anything that calls this must call Unmark() when done.
//
void Protect_Object(REBVAL *value, REBCNT flags)
{
    REBFRM *frame = VAL_FRAME(value);

    if (SERIES_GET_FLAG(FRAME_VARLIST(frame), SER_MARK))
        return; // avoid loop

    if (GET_FLAG(flags, PROT_SET))
        SERIES_SET_FLAG(FRAME_VARLIST(frame), SER_PROTECT);
    else
        SERIES_CLR_FLAG(FRAME_VARLIST(frame), SER_PROTECT);

    for (value = FRAME_KEY(frame, 1); NOT_END(value); value++) {
        Protect_Key(value, flags);
    }

    if (!GET_FLAG(flags, PROT_DEEP)) return;

    SERIES_SET_FLAG(FRAME_VARLIST(frame), SER_MARK); // recursion protection

    for (value = FRAME_VAR(frame, 1); NOT_END(value); value++) {
        Protect_Value(value, flags);
    }
}


//
//  Protect_Word_Value: C
//
static void Protect_Word_Value(REBVAL *word, REBCNT flags)
{
    REBVAL *key;
    REBVAL *val;

    if (ANY_WORD(word) && HAS_TARGET(word) && VAL_WORD_INDEX(word) > 0) {
        key = FRAME_KEY(AS_FRAME(VAL_WORD_TARGET(word)), VAL_WORD_INDEX(word));
        Protect_Key(key, flags);
        if (GET_FLAG(flags, PROT_DEEP)) {
            // Ignore existing mutability state, by casting away the const.
            // (Most routines should DEFINITELY not do this!)
            val = m_cast(REBVAL*, GET_VAR(word));
            Protect_Value(val, flags);
            Unmark(val);
        }
    }
    else if (ANY_PATH(word)) {
        REBCNT index;
        REBFRM *frame;
        if ((frame = Resolve_Path(word, &index))) {
            key = FRAME_KEY(frame, index);
            Protect_Key(key, flags);
            if (GET_FLAG(flags, PROT_DEEP)) {
                val = FRAME_VAR(frame, index);
                Protect_Value(val, flags);
                Unmark(val);
            }
        }
    }
}


//
//  Protect: C
// 
// Common arguments between protect and unprotect:
// 
//     1: value
//     2: /deep  - recursive
//     3: /words  - list of words
//     4: /values - list of values
// 
// Protect takes a HIDE parameter as #5.
//
static int Protect(struct Reb_Call *call_, REBCNT flags)
{
    REBVAL *val = D_ARG(1);

    // flags has PROT_SET bit (set or not)

    Check_Security(SYM_PROTECT, POL_WRITE, val);

    if (D_REF(2)) SET_FLAG(flags, PROT_DEEP);
    //if (D_REF(3)) SET_FLAG(flags, PROT_WORD);

    if (IS_WORD(val) || IS_PATH(val)) {
        Protect_Word_Value(val, flags); // will unmark if deep
        return R_ARG1;
    }

    if (IS_BLOCK(val)) {
        if (D_REF(3)) { // /words
            for (val = VAL_BLK_DATA(val); NOT_END(val); val++)
                Protect_Word_Value(val, flags);  // will unmark if deep
            return R_ARG1;
        }
        if (D_REF(4)) { // /values
            REBVAL *val2;
            REBVAL safe;
            for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
                if (IS_WORD(val)) {
                    // !!! Temporary and ugly cast; since we *are* PROTECT
                    // we allow ourselves to get mutable references to even
                    // protected values so we can no-op protect them.
                    val2 = m_cast(REBVAL*, GET_VAR(val));
                }
                else if (IS_PATH(val)) {
                    if (Do_Path_Throws(&safe, NULL, val, NULL))
                        fail (Error_No_Catch_For_Throw(&safe));

                    val2 = &safe;
                }
                else
                    val2 = val;

                Protect_Value(val2, flags);
                if (GET_FLAG(flags, PROT_DEEP)) Unmark(val2);
            }
            return R_ARG1;
        }
    }

    if (GET_FLAG(flags, PROT_HIDE)) fail (Error(RE_BAD_REFINES));

    Protect_Value(val, flags);

    if (GET_FLAG(flags, PROT_DEEP)) Unmark(val);

    return R_ARG1;
}


//
//  also: native [
//  
//  {Returns the first value, but also evaluates the second.}
//  
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(also)
{
    return R_ARG1;
}


//
//  all: native [
//  
//  {Shortcut AND. Returns NONE vs. TRUE (or last evaluation if it was TRUE?)}
//  
//      block [block!] "Block of expressions"
//  ]
//
REBNATIVE(all)
//
// ALL is effectively Rebol's "short-circuit AND".  Unsets do not vote either
// true or false...they are ignored.
// 
// To offer a more generically useful result than just TRUE or FALSE, it will
// use as a "truthy" value whatever the last evaluation in the chain was.  If
// there was no last value, but no conditionally false instance hit to break
// the chain, as in `all []` or `all [1 2 ()]`...it will return TRUE.
// 
// (Note: It would become a more costly operation to retain the last truthy
// value to return 2 in the case of `all [1 2 ()`]`, just to say it could.
// The overhead would undermine the raw efficiency of the operation.)
// 
// For the "falsy" value, ALL uses a NONE! rather than logic FALSE.  It's a
// historical design decision which has some benefits, but perhaps some
// drawbacks to those wishing to use it on logic values and stay in the
// logic domain.  (`all [true true]` => true, `all [false true]` is NONE!).
{
    REBSER *block = VAL_SERIES(D_ARG(1));
    REBCNT index = VAL_INDEX(D_ARG(1));

    SET_TRUE(D_OUT);

    while (index < SERIES_TAIL(block)) {
        DO_NEXT_MAY_THROW(index, D_OUT, block, index);
        if (index == THROWN_FLAG) return R_OUT_IS_THROWN;

        if (IS_UNSET(D_OUT)) continue;

        if (IS_CONDITIONAL_FALSE(D_OUT)) return R_NONE;
    }

    if (IS_UNSET(D_OUT)) return R_TRUE;

    return R_OUT;
}


//
//  any: native [
//  
//  {Shortcut OR, ignores unsets. Returns the first TRUE? result, or NONE.}
//  
//      block [block!] "Block of expressions"
//  ]
//
REBNATIVE(any)
//
// ANY is effectively Rebol's "short-circuit OR".  Unsets do not vote either
// true or false...they are ignored.
// 
// See ALL's notes about returning the last truthy value or NONE! (vs. FALSE)
// 
// The base case of `any []` is NONE! and not TRUE.  This might seem strange
// given that `all []` is TRUE.  But this ties more into what the questions
// they are used to ask about in practice: "Were all of these things not
// false?" as opposed to "Were any of these things true?"  It is also the
// case that `FALSE OR X OR Y` matches with `TRUE AND X AND Y` as the
// "seed" for not affecting the chain.
{
    REBSER *block = VAL_SERIES(D_ARG(1));
    REBCNT index = VAL_INDEX(D_ARG(1));

    while (index < SERIES_TAIL(block)) {
        DO_NEXT_MAY_THROW(index, D_OUT, block, index);
        if (index == THROWN_FLAG) return R_OUT_IS_THROWN;

        if (IS_UNSET(D_OUT)) continue;

        if (IS_CONDITIONAL_TRUE(D_OUT)) return R_OUT;
    }

    return R_NONE;
}


//
//  attempt: native [
//  
//  {Tries to evaluate a block and returns result or NONE on error.}
//  
//      block [block!]
//  ]
//
REBNATIVE(attempt)
{
    REBVAL * const block = D_ARG(1);

    REBOL_STATE state;
    REBFRM *error;

    PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) return R_NONE;

    if (DO_ARRAY_THROWS(D_OUT, block)) {
        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        // Throw name is in D_OUT, thrown value is held task local
        return R_OUT_IS_THROWN;
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return R_OUT;
}


//
//  break: native [
//  
//  {Breaks out of a loop, while, until, repeat, for-each, etc.}
//  
//      /with "Forces the loop function to return a value"
//      value [any-value!]
//      /return {(deprecated: mostly /WITH synonym, use THROW+CATCH if not)}
//      return-value [any-value!]
//  ]
//
REBNATIVE(break)
//
// BREAK is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :break`.
{
    REBVAL *value = D_REF(1) ? D_ARG(2) : (D_REF(3) ? D_ARG(4) : UNSET_VALUE);

    *D_OUT = *D_FUNC;

    CONVERT_NAME_TO_THROWN(D_OUT, value);

    return R_OUT_IS_THROWN;
}


//
//  case: native [
//  
//  {Evaluates each condition, and when true, evaluates what follows it.}
//  
//      block [block!] "Block of cases (conditions followed by values)"
//      /all {Evaluate all cases (do not stop at first TRUE? case)}
//  ]
//
REBNATIVE(case)
{
    // We leave D_ARG(1) alone, it is holding 'block' alive from GC
    REBSER *block = VAL_SERIES(D_ARG(1));
    REBCNT index = VAL_INDEX(D_ARG(1));

    // Save refinement to boolean to free up GC protected call frame slot
    REBFLG all = D_REF(2);

    // reuse refinement slot for GC safety (const pointer optimized out)
    REBVAL * const safe_temp = D_ARG(2);

    // condition result must survive across potential GC evaluations of
    // the body evaluation re-using `safe-temp`, but can be collapsed to a
    // flag as the full value of the condition is never returned.
    REBFLG matched;

    // CASE is in the same family as IF/UNLESS/EITHER, so if there is no
    // matching condition it will return UNSET!.  Set that as default.

    SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);

    while (index < SERIES_TAIL(block)) {

        DO_NEXT_MAY_THROW(index, safe_temp, block, index);

        if (index == THROWN_FLAG) {
            *D_OUT = *safe_temp; // is a RETURN, BREAK, THROW...
            return R_OUT_IS_THROWN;
        }

        // CASE statements are rather freeform as-is, and it seems most useful
        // to return an error on things like:
        //
        //     case [
        //         false [print "skipped"]
        //         false ; no matching body for condition
        //     ]

        if (index == END_FLAG) fail (Error(RE_PAST_END));

        // While unset is often a chance to "opt-out" of things, the condition
        // of an IF/UNLESS/EITHER is a spot where opting out is not allowed,
        // so it seems equally applicable to CASE.

        if (IS_UNSET(safe_temp)) fail (Error(RE_NO_RETURN));

        matched = IS_CONDITIONAL_TRUE(safe_temp);

        // We DO the next expression, rather than just assume it is a
        // literal block.  That allows you to write things like:
        //
        //     condition: true
        //     case [condition 10 + 20] ;-- returns 30
        //
        // But we need to DO regardless of the condition being true or
        // false.  Rebol2 would just skip over one item (the 10 in this
        // case) and get an error.  Code not in blocks must be evaluated
        // even if false, as it is with 'if false (print "eval'd")'
        //
        // If the source was a literal block then the Do_Next_May_Throw
        // will *probably* be a no-op, but consider infix operators:
        //
        //     case [true [stuff] + [more stuff]]
        //
        // Until such time as DO guarantees such things aren't legal,
        // CASE must evaluate block literals too.

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS) && !matched) {
            // case [true add 1 2] => 3
            // case [false add 1 2] => 2 ;-- in Rebol2
            index++;

            // forgets the last evaluative result for a TRUE condition
            // when /ALL is set (instead of keeping it to return)
            SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);
            continue;
        }
    #endif

        DO_NEXT_MAY_THROW(index, safe_temp, block, index);

        if (index == THROWN_FLAG) {
            *D_OUT = *safe_temp; // is a RETURN, BREAK, THROW...
            return R_OUT_IS_THROWN;
        }

        if (index == END_FLAG) {
        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)) {
                // case [first [a b c]] => true ;-- in Rebol2
                return R_TRUE;
            }
        #endif

            // case [first [a b c]] => **error**
            fail (Error(RE_PAST_END));
        }

        if (matched) {

            if (IS_BLOCK(safe_temp)) {
                // The classical implementation of CASE is defined to give two
                // evals for things like:
                //
                //     stuff: [print "This will be printed"]
                //     case [true stuff]
                //
                // This puts it more closely in the spirit of being a kind of
                // "optimized IF-ELSE" as `if true stuff` would also behave
                // in the manner of running that block.

                if (DO_ARRAY_THROWS(D_OUT, safe_temp))
                    return R_OUT_IS_THROWN;
            }
            else
                *D_OUT = *safe_temp;

        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)) {
                if (IS_UNSET(D_OUT)) {
                    // case [true [] false [1 + 2]] => true ;-- in Rebol2
                    SET_TRUE(D_OUT);
                }
            }
        #endif

            // One match is enough to return the result now, unless /ALL
            if (!all) return R_OUT;
        }
    }

    // Returns the evaluative result of the last body whose condition was
    // conditionally true, or defaults to UNSET if there weren't any
    // (or NONE in legacy mode)

    return R_OUT;
}


//
//  catch: native [
//  
//  {Catches a throw from a block and returns its value.}
//  
//      block [block!] "Block to evaluate"
//      /name "Catches a named throw"
//      name-list [block! word! any-function! object!] 
//      "Names to catch (single name if not block)"
//      /quit "Special catch for QUIT native"
//      /any {Catch all throws except QUIT (can be used with /QUIT)}
//      /with "Handle thrown case with code"
//      handler [block! any-function!] 
//      "If FUNCTION!, spec matches [value name]"
//  ]
//
REBNATIVE(catch)
//
// There's a refinement for catching quits, and CATCH/ANY will not alone catch
// it (you have to CATCH/ANY/QUIT).  Currently the label for quitting is the
// NATIVE! function value for QUIT.
{
    REBVAL * const block = D_ARG(1);

    const REBOOL named = D_REF(2);
    REBVAL * const name_list = D_ARG(3);

    // We save the values into booleans (and reuse their GC-protected slots)
    const REBOOL quit = D_REF(4);
    const REBOOL any = D_REF(5);

    const REBOOL with = D_REF(6);
    REBVAL * const handler = D_ARG(7);

    // /ANY would override /NAME, so point out the potential confusion
    if (any && named) fail (Error(RE_BAD_REFINES));

    if (DO_ARRAY_THROWS(D_OUT, block)) {
        if (
            (any && (!IS_NATIVE(D_OUT) || VAL_FUNC_CODE(D_OUT) != &N_quit))
            || (quit && (IS_NATIVE(D_OUT) && VAL_FUNC_CODE(D_OUT) == &N_quit))
        ) {
            goto was_caught;
        }

        if (named) {
            // We use equal? by way of Compare_Modify_Values, and re-use the
            // refinement slots for the mutable space
            REBVAL * const temp1 = D_ARG(4);
            REBVAL * const temp2 = D_ARG(5);

            // !!! The reason we're copying isn't so the OPT_VALUE_THROWN bit
            // won't confuse the equality comparison...but would it have?

            if (IS_BLOCK(name_list)) {
                // Test all the words in the block for a match to catch
                REBVAL *candidate = VAL_BLK_DATA(name_list);
                for (; NOT_END(candidate); candidate++) {
                    // !!! Should we test a typeset for illegal name types?
                    if (IS_BLOCK(candidate))
                        fail (Error(RE_INVALID_ARG, name_list));

                    *temp1 = *candidate;
                    *temp2 = *D_OUT;

                    // Return the THROW/NAME's arg if the names match
                    // !!! 0 means equal?, but strict-equal? might be better
                    if (Compare_Modify_Values(temp1, temp2, 0))
                        goto was_caught;
                }
            }
            else {
                *temp1 = *name_list;
                *temp2 = *D_OUT;

                // Return the THROW/NAME's arg if the names match
                // !!! 0 means equal?, but strict-equal? might be better
                if (Compare_Modify_Values(temp1, temp2, 0))
                    goto was_caught;
            }
        }
        else {
            // Return THROW's arg only if it did not have a /NAME supplied
            if (IS_NONE(D_OUT))
                goto was_caught;
        }

        // Throw name is in D_OUT, thrown value is held task local
        return R_OUT_IS_THROWN;
    }

    return R_OUT;

was_caught:
    if (with) {
        // We again re-use the refinement slots, but this time as mutable
        // space protected from GC for the handler's arguments
        REBVAL *thrown_arg = D_ARG(4);
        REBVAL *thrown_name = D_ARG(5);

        CATCH_THROWN(thrown_arg, D_OUT);
        *thrown_name = *D_OUT; // THROWN bit cleared by TAKE_THROWN_ARG

        if (IS_BLOCK(handler)) {
            // There's no way to pass args to a block (so just DO it)
            if (DO_ARRAY_THROWS(D_OUT, handler))
                return R_OUT_IS_THROWN;

            return R_OUT;
        }
        else if (ANY_FUNC(handler)) {
            REBVAL *param = BLK_SKIP(VAL_FUNC_PARAMLIST(handler), 1);

            if (
                (VAL_FUNC_NUM_PARAMS(handler) == 0)
                || IS_REFINEMENT(VAL_FUNC_PARAM(handler, 1))
            ) {
                // If the handler is zero arity or takes a first parameter
                // that is a refinement, call it with no arguments
                //
                if (Apply_Func_Throws(D_OUT, handler, NULL))
                    return R_OUT_IS_THROWN;
            }
            else if (
                (VAL_FUNC_NUM_PARAMS(handler) == 1)
                || IS_REFINEMENT(VAL_FUNC_PARAM(handler, 2))
            ) {
                // If the handler is arity one (with a non-refinement
                // parameter), or a greater arity with a second parameter that
                // is a refinement...call it with *just* the thrown value.
                //
                if (Apply_Func_Throws(D_OUT, handler, thrown_arg, NULL))
                    return R_OUT_IS_THROWN;
            }
            else {
                // For all other handler signatures, try passing both the
                // thrown arg and the thrown name.  Let Apply take care of
                // checking that the arguments are legal for the call.
                //
                if (Apply_Func_Throws(
                    D_OUT, handler, thrown_arg, thrown_name, NULL
                )) {
                    return R_OUT_IS_THROWN;
                }
            }

            return R_OUT;
        }
    }

    // If no handler, just return the caught thing
    CATCH_THROWN(D_OUT, D_OUT);
    return R_OUT;
}


//
//  throw: native [
//  
//  "Throws control back to a previous catch."
//  
//      value [any-value!] "Value returned from catch"
//      /name "Throws to a named catch"
//      name-value [word! any-function! object!]
//  ]
//
REBNATIVE(throw)
{
    REBVAL * const value = D_ARG(1);
    REBOOL named = D_REF(2);
    REBVAL * const name_value = D_ARG(3);

    if (IS_ERROR(value)) {
        // We raise an alert from within the implementation of throw for
        // trying to use it to trigger errors, because if THROW just didn't
        // take errors in the spec it wouldn't guide what *to* use.
        //
        fail (Error(RE_USE_FAIL_FOR_ERROR, value));

        // Note: Caller can put the ERROR! in a block or use some other
        // such trick if it wants to actually throw an error.
        // (Better than complicating via THROW/ERROR-IS-INTENTIONAL!)
    }

    if (named) {
        // blocks as names would conflict with name_list feature in catch
        assert(!IS_BLOCK(name_value));
        *D_OUT = *name_value;
    }
    else {
        // None values serving as representative of THROWN() means "no name"

        // !!! This convention might be a bit "hidden" while debugging if
        // one misses the THROWN() bit.  But that's true of THROWN() values
        // in general.  Debug output should make noise about THROWNs
        // whenever it sees them.

        SET_NONE(D_OUT);
    }

    CONVERT_NAME_TO_THROWN(D_OUT, value);

    // Throw name is in D_OUT, thrown value is held task local
    return R_OUT_IS_THROWN;
}


//
//  comment: native/frameless [
//
//  {Ignores the argument value and returns nothing (with no evaluations).}
//
//      :value [block! any-string! binary! any-scalar!]
//          "Literal value to be ignored."
//  ]
//
REBNATIVE(comment)
{
    PARAM(1, value);

    if (D_FRAMELESS) {
        D_VALUE = BLK_SKIP(D_ARRAY, D_INDEX);

        if (IS_END(D_VALUE))
            fail (Error_No_Arg(D_LABEL_SYM, PAR(value)));

        if (ANY_EVAL(D_VALUE))
            fail (Error_Arg_Type(D_LABEL_SYM, PAR(value), Type_Of(D_VALUE)));

        SET_UNSET(D_OUT);
        D_INDEX++;

        return R_OUT;
    }
    else {
        // Framed!  All the work was already done (at the cost of setting up
        // state that would just have to be torn down).  Since comment has
        // no refinements, this should only be called in debug modes.

        return R_UNSET;
    }
}


//
//  compose: native/frameless [
//  
//  {Evaluates a block of expressions, only evaluating parens, and returns a block.}
//  
//      value "Block to compose"
//      /deep "Compose nested blocks"
//      /only 
//      {Insert a block as a single value (not the contents of the block)}
//      /into {Output results into a series with no intermediate storage}
//      out [any-array! any-string! binary!]
//  ]
//
REBNATIVE(compose)
//
// !!! Should 'compose quote (a (1 + 2) b)' give back '(a 3 b)' ?
// !!! What about 'compose quote a/(1 + 2)/b' ?
{
    PARAM(1, value);
    REFINE(2, deep);
    REFINE(3, only);
    REFINE(4, into);
    PARAM(5, out);

    if (D_FRAMELESS) {
        DO_NEXT_MAY_THROW(D_INDEX, D_CELL, D_ARRAY, D_INDEX);

        if (D_INDEX == END_FLAG)
            fail (Error_No_Arg(D_LABEL_SYM, PAR(value)));

        if (D_INDEX == THROWN_FLAG) {
            *D_OUT = *D_CELL;
            return R_OUT_IS_THROWN;
        }

        if (IS_UNSET(D_CELL))
            fail (Error_Arg_Type(D_LABEL_SYM, PAR(value), Type_Of(D_CELL)));

        if (!IS_BLOCK(D_CELL)) {
            *D_OUT = *D_CELL;
            return R_OUT;
        }

        if (Compose_Values_Throws(
            D_OUT, VAL_BLK_HEAD(D_CELL), FALSE, FALSE, FALSE
        )) {
            return R_OUT_IS_THROWN;
        }

        return R_OUT;
    }

    // Only composes BLOCK!, all other arguments evaluate to themselves
    //
    if (!IS_BLOCK(ARG(value))) return R_ARG1;

    // Compose_Values_Throws() expects `out` to contain the target if it is
    // passed TRUE as the `into` flag.
    //
    if (REF(into)) *D_OUT = *ARG(out);

    if (Compose_Values_Throws(
        D_OUT, VAL_BLK_HEAD(ARG(value)), REF(deep), REF(only), REF(into)
    )) {
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


//
//  continue: native [
//  
//  "Throws control back to top of loop."
//  
//      /with {Act as if loop body finished current evaluation with a value}
//      value [any-value!]
//  ]
//
REBNATIVE(continue)
//
// CONTINUE is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :continue`.
{
    REBVAL *value = D_REF(1) ? D_ARG(2) : UNSET_VALUE;

    *D_OUT = *D_FUNC;

    CONVERT_NAME_TO_THROWN(D_OUT, value);

    return R_OUT_IS_THROWN;
}


//
//  do: native [
//  
//  {Evaluates a block of source code (directly or fetched according to type)}
//  
//      source [unset! none! block! paren! string! binary! url! file! tag! 
//      error! any-function!]
//      /args {If value is a script, this will set its system/script/args}
//      arg "Args passed to a script (normally a string)"
//      /next {Do next expression only, return it, update block variable}
//      var [word! none!] "Variable updated with new block position"
//  ]
//
REBNATIVE(do)
{
    PARAM(1, value);
    REFINE(2, args);
    PARAM(3, arg);
    REFINE(4, next);
    PARAM(5, var); // if NONE!, DO/NEXT only but no var update

    switch (VAL_TYPE(ARG(value))) {
    case REB_UNSET:
        // useful for `do if ...` types of scenarios
        return R_UNSET;

    case REB_NONE:
        // useful for `do all ...` types of scenarios
        return R_NONE;

    case REB_BLOCK:
    case REB_PAREN:
        if (REF(next)) {
            DO_NEXT_MAY_THROW(
                VAL_INDEX(ARG(value)), // updates index of value in call frame
                D_OUT,
                VAL_SERIES(ARG(value)),
                VAL_INDEX(ARG(value))
            );

            if (VAL_INDEX(ARG(value)) == THROWN_FLAG) {
                // the throw should make the value irrelevant, but if caught
                // then have it indicate the start of the thrown expression

                // !!! What if the block was mutated, and D_ARG(1) is no
                // longer actually the expression that started the throw?

                if (!IS_NONE(ARG(var)))
                    Set_Var(ARG(var), ARG(value));
                return R_OUT_IS_THROWN;
            }

            if (VAL_INDEX(ARG(value)) == END_FLAG) {
                // If we hit the end, we always want to return unset.
                if (!IS_NONE(ARG(var))) {
                    // Set a var for DO/NEXT only if we were asked to.
                    VAL_INDEX(ARG(value)) = VAL_TAIL(ARG(value));
                    Set_Var(ARG(var), ARG(value));
                }
                return R_UNSET;
            }

            if (!IS_NONE(ARG(var)))
                Set_Var(ARG(var), ARG(value)); // "continuation" of block
            return R_OUT;
        }

        if (DO_ARRAY_THROWS(D_OUT, ARG(value)))
            return R_OUT_IS_THROWN;

        return R_OUT;

    case REB_BINARY:
    case REB_STRING:
    case REB_URL:
    case REB_FILE:
    case REB_TAG:
        //
        // DO native and system/intrinsic/do* must use same arg list:
        //
        if (Do_Sys_Func_Throws(
            D_OUT,
            SYS_CTX_DO_P,
            ARG(value),
            ARG(args),
            ARG(arg),
            ARG(next),
            ARG(var),
            NULL
        )) {
            return R_OUT_IS_THROWN;
        }
        return R_OUT;

    case REB_ERROR:
        //
        // FAIL is the preferred operation for triggering errors, as it has
        // a natural behavior for blocks passed to construct readable messages
        // and "FAIL X" more clearly communicates a failure than "DO X"
        // does.  However DO of an ERROR! would have to raise an error
        // anyway, so it might as well raise the one it is given.
        //
        fail (VAL_FRAME(ARG(value)));

    case REB_TASK:
        Do_Task(ARG(value));
        return R_ARG1;
    }

    // Note: it is not possible to write a wrapper function in Rebol
    // which can do what EVAL can do for types that consume arguments
    // (like SET-WORD!, SET-PATH! and FUNCTION!).  DO used to do this for
    // functions only, EVAL generalizes it.
    //
    // !!! The LEGACY mode for DO that allows it to run functions is,
    // like EVAL, implemented as part of the evaluator by recognizing
    // the &N_do native function pointer.
    //
    fail (Error(RE_USE_EVAL_FOR_EVAL));
}


//
//  eval: native [
//  
//  {(Special) Process received value *inline* as the evaluator loop would.}
//  
//      value [any-value!] 
//      {BLOCK! passes-thru, FUNCTION! runs, SET-WORD! assigns...}
//  ]
//
REBNATIVE(eval)
{
    // There should not be any way to call this actual function, because it
    // will be intercepted by recognizing its identity in the evaluator loop
    // itself (required to do the "magic")

    fail (Error(RE_MISC));
}


//
//  exit: native [
//  
//  {Leave whatever enclosing Rebol state EXIT's block *actually* runs in.}
//  
//      /with "Result for enclosing state (default is UNSET!)"
//      value [any-value!]
//  ]
//
REBNATIVE(exit)
//
// EXIT is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :exit`.
{
    *D_OUT = *D_FUNC;

    CONVERT_NAME_TO_THROWN(D_OUT, D_REF(1) ? D_ARG(2) : UNSET_VALUE);

    return R_OUT_IS_THROWN;
}


//
//  fail: native [
//  
//  {Interrupts execution by reporting an error (a TRAP can intercept it).}
//  
//      reason [error! string! block!] 
//      "ERROR! value, message string, or failure spec"
//  ]
//
REBNATIVE(fail)
{
    REBVAL * const reason = D_ARG(1);

    if (IS_ERROR(reason)) {
        fail (VAL_FRAME(reason));
    }
    else if (IS_STRING(reason) || IS_BLOCK(reason)) {
        // Ultimately we'd like FAIL to use some clever error-creating
        // dialect when passed a block, maybe something like:
        //
        //     fail [<invalid-key> {The key} key-name: key {is invalid}]
        //
        // That could provide an error ID, the format message, and the
        // values to plug into the slots to make the message...which could
        // be extracted from the error if captured (e.g. error/id and
        // `error/key-name`.  Another option would be something like:
        //
        //     fail/with [{The key} :key-name {is invalid}] [key-name: key]
        //
        if (IS_BLOCK(reason)) {
            // Check to make sure we're only drawing from the limited types
            // we accept (reserving room for future dialect expansion)
            REBCNT index = VAL_INDEX(reason);
            for (; index < SERIES_LEN(VAL_SERIES(reason)); index++) {
                REBVAL *item = BLK_SKIP(VAL_SERIES(reason), index);
                if (IS_STRING(item) || IS_SCALAR(item) || IS_PAREN(item))
                    continue;

                // We don't want to dispatch functions directly (use parens)

                // !!! This keeps the option open of being able to know that
                // strings that appear in the block appear in the error
                // message so it can be templated.

                if (IS_WORD(item)) {
                    const REBVAL *var = TRY_GET_VAR(item);
                    if (!var || !ANY_FUNC(var))
                        continue;
                }

                // The only way to tell if a path resolves to a function
                // or not is to actually evaluate it, and we are delegating
                // to Reduce_Block ATM.  For now we force you to use a PAREN!
                //
                //     fail [{Erroring on} (the/safe/side) {for now.}]

                fail (Error(RE_LIMITED_FAIL_INPUT));
            }

            // We just reduce and form the result, but since we allow PAREN!
            // it means you can put in pretty much any expression.
            if (Reduce_Block_Throws(
                reason, VAL_SERIES(reason), VAL_INDEX(reason), FALSE
            )) {
                *D_OUT = *reason;
                return R_OUT_IS_THROWN;
            }

            Val_Init_String(reason, Copy_Form_Value(reason, 0));
        }

        if (Make_Error_Object_Throws(D_OUT, reason)) {
            // Throw name is in D_OUT, thrown value is held task local
            return R_OUT_IS_THROWN;
        }

        fail (VAL_FRAME(D_OUT));
    }

    DEAD_END;
}


static REB_R If_Unless_Core(struct Reb_Call *call_, REBFLG trigger) {
    PARAM(1, condition);
    PARAM(2, branch);
    REFINE(3, only);

    assert((trigger == TRUE) || (trigger == FALSE));

    if (D_FRAMELESS) {
        //
        // First evaluate the condition into D_OUT
        //
        DO_NEXT_MAY_THROW(D_INDEX, D_OUT, D_ARRAY, D_INDEX);

        if (D_INDEX == END_FLAG)
            fail (Error_No_Arg(D_LABEL_SYM, PAR(condition)));

        if (D_INDEX == THROWN_FLAG)
            return R_OUT_IS_THROWN;

        if (IS_UNSET(D_OUT))
            fail (Error_Arg_Type(D_LABEL_SYM, PAR(condition), Type_Of(D_OUT)));

        if (IS_CONDITIONAL_TRUE(D_OUT) == trigger) {
            //
            // Matched what we were looking for (TRUE for IF, FALSE for UNLESS)
            // We can now evaluate the branch into D_OUT.
            //
            DO_NEXT_MAY_THROW(D_INDEX, D_OUT, D_ARRAY, D_INDEX);

            if (D_INDEX == END_FLAG)
                fail (Error_No_Arg(D_LABEL_SYM, PAR(branch)));

            if (D_INDEX == THROWN_FLAG)
                return R_OUT_IS_THROWN;

            // We know there is no /ONLY because frameless never runs
            // when you have refinements.  Hence always evaluate blocks.
            //
            if (IS_BLOCK(D_OUT)) {
                if (DO_ARRAY_THROWS(D_OUT, D_OUT)) // array = out is safe
                    return R_OUT_IS_THROWN;
                return R_OUT;
            }

            // Non-blocks return as-is.
            //
            return R_OUT;
        }

        // Even though we know we don't want to take the branch, we still have
        // to evaluate it (which is the behavior that would have happened if
        // a frame had been built for us).
        //
        DO_NEXT_MAY_THROW(D_INDEX, D_OUT, D_ARRAY, D_INDEX);

        if (D_INDEX == END_FLAG)
            fail (Error_No_Arg(D_LABEL_SYM, PAR(branch)));

        if (D_INDEX == THROWN_FLAG)
            return R_OUT_IS_THROWN;

        SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);
        return R_OUT;
    }

    // The framed variation uses the same logic, but is simpler.  This will
    // run in debug or trace situations, as well as if /ONLY is used.
    //
    if (IS_CONDITIONAL_TRUE(ARG(condition)) == trigger) {
        if (REF(only) || !IS_BLOCK(ARG(branch))) {
            *D_OUT = *ARG(branch);
        }
        else if (DO_ARRAY_THROWS(D_OUT, ARG(branch)))
            return R_OUT_IS_THROWN;
    }
    else
        SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);

    return R_OUT;
}


//
//  if: native/frameless [
//  
//  {If TRUE? condition, return branch value; evaluate blocks by default.}
//  
//      condition
//      branch [any-value!]
//      /only "Return block branches literally instead of evaluating them."
//  ]
//
REBNATIVE(if)
{
    return If_Unless_Core(call_, TRUE);
}


//
//  unless: native/frameless [
//
//  {If FALSE? condition, return branch value; evaluate blocks by default.}
//
//      condition
//      branch [any-value!]
//      /only "Return block branches literally instead of evaluating them."
//  ]
//
REBNATIVE(unless)
{
    return If_Unless_Core(call_, FALSE);
}


//
//  either: native/frameless [
//
//  {If TRUE condition? first branch, else second; evaluate blocks by default.}
//
//      condition
//      true-branch [any-value!]
//      false-branch [any-value!]
//      /only "Return block arg instead of evaluating it."
//  ]
//
REBNATIVE(either)
{
    PARAM(1, condition);
    PARAM(2, true_branch);
    PARAM(3, false_branch);
    REFINE(4, only);

    if (D_FRAMELESS) {
        //
        // First evaluate the condition into D_OUT
        //
        DO_NEXT_MAY_THROW(D_INDEX, D_OUT, D_ARRAY, D_INDEX);

        if (D_INDEX == END_FLAG)
            fail (Error_No_Arg(D_LABEL_SYM, PAR(condition)));

        if (D_INDEX == THROWN_FLAG)
            return R_OUT_IS_THROWN;

        if (IS_UNSET(D_OUT))
            fail (Error_Arg_Type(D_LABEL_SYM, PAR(condition), Type_Of(D_OUT)));

        // If conditionally true, we want the protected D_OUT to be used for
        // the true branch evaluation, and use D_CELL for scratch space to
        // do the false branch into.  If false, we want D_OUT to be used for
        // the false branch evaluation with the true branch writing into
        // D_CELL as scratch space.
        //
        if (IS_CONDITIONAL_TRUE(D_OUT)) {
            DO_NEXT_MAY_THROW(D_INDEX, D_OUT, D_ARRAY, D_INDEX);

            if (D_INDEX == END_FLAG)
                fail (Error_No_Arg(D_LABEL_SYM, PAR(true_branch)));

            if (D_INDEX == THROWN_FLAG)
                return R_OUT_IS_THROWN;

            DO_NEXT_MAY_THROW(D_INDEX, D_CELL, D_ARRAY, D_INDEX);

            if (D_INDEX == END_FLAG)
                fail (Error_No_Arg(D_LABEL_SYM, PAR(false_branch)));

            if (D_INDEX == THROWN_FLAG) {
                *D_OUT = *D_CELL;
                return R_OUT_IS_THROWN;
            }
        }
        else {
            DO_NEXT_MAY_THROW(D_INDEX, D_CELL, D_ARRAY, D_INDEX);

            if (D_INDEX == END_FLAG)
                fail (Error_No_Arg(D_LABEL_SYM, PAR(true_branch)));

            if (D_INDEX == THROWN_FLAG) {
                *D_OUT = *D_CELL;
                return R_OUT_IS_THROWN;
            }

            DO_NEXT_MAY_THROW(D_INDEX, D_OUT, D_ARRAY, D_INDEX);

            if (D_INDEX == END_FLAG)
                fail (Error_No_Arg(D_LABEL_SYM, PAR(false_branch)));

            if (D_INDEX == THROWN_FLAG)
                return R_OUT_IS_THROWN;
        }

        // We know at this point that D_OUT contains what we want to be
        // working with for the output, and we also know there's no /ONLY.
        //
        if (IS_BLOCK(D_OUT)) {
             if (DO_ARRAY_THROWS(D_OUT, D_OUT)) // array = out is safe
                return R_OUT_IS_THROWN;
            return R_OUT;
        }

        // Return non-blocks as-is
        //
        return R_OUT;
    }

    // The framed variation uses the same logic, but is simpler.  This will
    // run in debug or trace situations, as well as if /ONLY is used.
    //
    if (IS_CONDITIONAL_TRUE(ARG(condition))) {
        if (REF(only) || !IS_BLOCK(ARG(true_branch))) {
            *D_OUT = *ARG(true_branch);
        }
        else if (DO_ARRAY_THROWS(D_OUT, ARG(true_branch)))
            return R_OUT_IS_THROWN;
    }
    else {
        if (REF(only) || !IS_BLOCK(ARG(false_branch))) {
            *D_OUT = *ARG(false_branch);
        }
        else if (DO_ARRAY_THROWS(D_OUT, ARG(false_branch)))
            return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


//
//  protect: native [
//  
//  {Protect a series or a variable from being modified.}
//  
//      value [word! any-series! bitset! map! object! module!]
//      /deep "Protect all sub-series/objects as well"
//      /words "Process list as words (and path words)"
//      /values "Process list of values (implied GET)"
//      /hide "Hide variables (avoid binding and lookup)"
//  ]
//
REBNATIVE(protect)
{
    PARAM(1, value);
    REFINE(2, deep);
    REFINE(3, words);
    REFINE(4, values);
    REFINE(5, hide);

    REBCNT flags = FLAGIT(PROT_SET);

    if (REF(hide)) SET_FLAG(flags, PROT_HIDE);
    else SET_FLAG(flags, PROT_WORD); // there is no unhide

    // accesses arguments 1 - 4
    return Protect(call_, flags);
}


//
//  unprotect: native [
//  
//  {Unprotect a series or a variable (it can again be modified).}
//  
//      value [word! any-series! bitset! map! object! module!]
//      /deep "Protect all sub-series as well"
//      /words "Block is a list of words"
//      /values "Process list of values (implied GET)"
//  ]
//
REBNATIVE(unprotect)
{
    // accesses arguments 1 - 4
    return Protect(call_, FLAGIT(PROT_WORD));
}


//
//  reduce: native [
//  
//  {Evaluates expressions and returns multiple results.}
//  
//      value
//      /no-set "Keep set-words as-is. Do not set them."
//      /only "Only evaluate words and paths, not functions"
//      words [block! none!] "Optional words that are not evaluated (keywords)"
//      
//      /into {Output results into a series with no intermediate storage}
//      out [any-array! any-string! binary!]
//  ]
//
REBNATIVE(reduce)
{
    if (IS_BLOCK(D_ARG(1))) {
        REBSER *ser = VAL_SERIES(D_ARG(1));
        REBCNT index = VAL_INDEX(D_ARG(1));
        REBOOL into = D_REF(5);

        if (into)
            *D_OUT = *D_ARG(6);

        if (D_REF(2)) {
            if (Reduce_Block_No_Set_Throws(D_OUT, ser, index, into))
                return R_OUT_IS_THROWN;
        }
        else if (D_REF(3))
            Reduce_Only(D_OUT, ser, index, D_ARG(4), into);
        else {
            if (Reduce_Block_Throws(D_OUT, ser, index, into))
                return R_OUT_IS_THROWN;
        }

        return R_OUT;
    }

    return R_ARG1;
}


//
//  return: native [
//  
//  "Returns a value from a function."
//  
//      value [any-value!]
//  ]
//
REBNATIVE(return)
//
// There is a RETURN native defined, and its native function spec is
// utilized to create the appropriate help and calling protocol
// information for values that have overridden its VAL_FUNC_CODE
// slot with a VAL_FUNC_RETURN_TO spec.
// 
// However: this native is unset and its actual code body should
// never be able to be called.  The non-definitional return construct
// that people should use if they need it would be EXIT and EXIT/WITH
{
    panic (Error(RE_MISC));

    return R_NONE;
}


//
//  switch: native [
//  
//  {Selects a choice and evaluates the block that follows it.}
//  
//      value "Target value"
//      cases [block!] "Block of cases to check"
//      /default case "Default case if no others found"
//      /all "Evaluate all matches (not just first one)"
//      /strict {Use STRICT-EQUAL? when comparing cases instead of EQUAL?}
//  ]
//
REBNATIVE(switch)
{
    REBVAL * const value = D_ARG(1);
    REBVAL * const cases = D_ARG(2);
    // has_default implied by default_case not being none
    REBVAL * const default_case = D_ARG(4);
    REBOOL all = D_REF(5);
    REBOOL strict = D_REF(6);

    REBOOL found = FALSE;

    REBVAL *item = VAL_BLK_DATA(cases);

    SET_UNSET_UNLESS_LEGACY_NONE(D_OUT); // default return if no cases run

    for (; NOT_END(item); item++) {

        // The way SWITCH works with blocks is that blocks are considered
        // bodies to match for other value types, so you can't use them
        // as case keys themselves.  They'll be skipped until we find
        // a non-block case we want to match.

        if (IS_BLOCK(item)) {
            // Each time we see a block that we don't take, we reset
            // the output to UNSET!...because we only leak evaluations
            // out the bottom of the switch if no block would catch it

            SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);
            continue;
        }

        // GET-WORD!, GET-PATH!, and PAREN! are evaluated (an escaping
        // mechanism as in lit-quotes of function specs to avoid quoting)
        // You can still evaluate to one of these, e.g. `(quote :foo)` to
        // use parens to produce a GET-WORD! to test against.

        if (IS_PAREN(item)) {

        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_NO_SWITCH_EVALS)) {
                // !!! Note this as a delta in the legacy log
                *D_OUT = *item;
                goto compare_values;
            }
        #endif

            if (DO_ARRAY_THROWS(D_OUT, item))
                return R_OUT_IS_THROWN;
        }
        else if (IS_GET_WORD(item)) {

        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_NO_SWITCH_EVALS)) {
                // !!! Note this as a delta in the legacy log
                *D_OUT = *item;
                goto compare_values;
            }
        #endif

            GET_VAR_INTO(D_OUT, item);
        }
        else if (IS_GET_PATH(item)) {

        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_NO_SWITCH_EVALS)) {
                // !!! Note this as a delta in the legacy log
                *D_OUT = *item;
                goto compare_values;
            }
        #endif

            if (Do_Path_Throws(D_OUT, NULL, item, NULL))
                return R_OUT;
        }
        else {
            // Even if we're just using the item literally, we need to copy
            // it from the block the user loaned us...because the type
            // coercion in Compare_Modify_Values could mutate it.

            *D_OUT = *item;
        }

    #if !defined(NDEBUG)
    compare_values: // only used by LEGACY(OPTIONS_NO_SWITCH_EVALS)
    #endif

        // It's okay that we are letting the comparison change `value`
        // here, because equality is supposed to be transitive.  So if it
        // changes 0.01 to 1% in order to compare it, anything 0.01 would
        // have compared equal to so will 1%.  (That's the idea, anyway,
        // required for `a = b` and `b = c` to properly imply `a = c`.)

        if (!Compare_Modify_Values(value, D_OUT, strict ? 2 : 0))
            continue;

        // Skip ahead to try and find a block, to treat as code

        while (!IS_BLOCK(item)) {
            if (IS_END(item)) break;
            item++;
        }

        found = TRUE;

        if (DO_ARRAY_THROWS(D_OUT, item))
            return R_OUT_IS_THROWN;

        // Only keep processing if the /ALL refinement was specified

        if (!all) return R_OUT;
    }

    if (!found && IS_BLOCK(default_case)) {
        if (DO_ARRAY_THROWS(D_OUT, default_case))
            return R_OUT_IS_THROWN;

        return R_OUT;
    }

    #if !defined(NDEBUG)
        // The previous answer to `switch 1 [1]` was a NONE!.  This was
        // a candidate for marking as an error, however the new idea is to
        // let cases that do not have a block after them be evaluated
        // (if necessary) and the last one to fall through and be the
        // result.  This offers a nicer syntax for a default, especially
        // when PAREN! is taken into account.
        //
        // However, running in legacy compatibility mode we need to squash
        // the value into a NONE! so it doesn't fall through.
        //
        if (LEGACY(OPTIONS_NO_SWITCH_FALLTHROUGH)) {
            if (!IS_NONE(D_OUT)) {
                // !!! Note this difference in legacy log
            }
            return R_NONE;
        }
    #endif

    return R_OUT;
}


//
//  trap: native [
//  
//  {Tries to DO a block, trapping error as return value (if one is raised).}
//  
//      block [block!]
//      /with "Handle error case with code"
//      handler [block! any-function!] 
//      "If FUNCTION!, spec allows [error [error!]]"
//  ]
//
REBNATIVE(trap)
{
    PARAM(1, block);
    REFINE(2, with);
    PARAM(3, handler);

    REBOL_STATE state;
    REBFRM *error;

    PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        if (REF(with)) {
            if (IS_BLOCK(ARG(handler))) {
                // There's no way to pass 'error' to a block (so just DO it)
                if (DO_ARRAY_THROWS(D_OUT, ARG(handler)))
                    return R_OUT_IS_THROWN;

                return R_OUT;
            }
            else if (ANY_FUNC(ARG(handler))) {
                if (
                    (VAL_FUNC_NUM_PARAMS(ARG(handler)) == 0)
                    || IS_REFINEMENT(VAL_FUNC_PARAM(ARG(handler), 1))
                ) {
                    // Arity zero handlers (or handlers whose first
                    // parameter is a refinement) we call without the ERROR!
                    //
                    if (Apply_Func_Throws(D_OUT, ARG(handler), NULL))
                        return R_OUT_IS_THROWN;
                }
                else {
                    REBVAL arg;
                    Val_Init_Error(&arg, error);

                    // If the handler takes at least one parameter that
                    // isn't a refinement, try passing it the ERROR! we
                    // trapped.  Apply will do argument checking.
                    //
                    if (Apply_Func_Throws(D_OUT, ARG(handler), &arg, NULL))
                        return R_OUT_IS_THROWN;
                }

                return R_OUT;
            }

            panic (Error(RE_MISC)); // should not be possible (type-checking)
        }

        Val_Init_Error(D_OUT, error);
        return R_OUT;
    }

    if (DO_ARRAY_THROWS(D_OUT, ARG(block))) {
        // Note that we are interested in when errors are raised, which
        // causes a tricky C longjmp() to the code above.  Yet a THROW
        // is different from that, and offers an opportunity to each
        // DO'ing stack level along the way to CATCH the thrown value
        // (with no need for something like the PUSH_TRAP above).
        //
        // We're being given that opportunity here, but doing nothing
        // and just returning the THROWN thing for other stack levels
        // to look at.  For the construct which does let you catch a
        // throw, see REBNATIVE(catch), which has code for this case.

        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
        return R_OUT_IS_THROWN;
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return R_OUT;
}

//
//  File: %u-dialect.c
//  Summary: "support for dialecting"
//  Section: utility
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
#include "reb-dialect.h"

typedef struct Reb_Dialect_Parse {
    REBCTX *dialect;    // dialect object
    REBARR *fargs;      // formal arg block
    REBCNT fargi;       // start index in fargs
    REBARR *args;       // argument block
    REBCNT argi;        // current arg index
    REBCNT cmd;         // command id
    REBINT len;         // limit of current command
    REBARR *out;        // result block
    REBINT outi;        // result block index
    REBINT flags;
    REBINT missed;      // counter of arg misses
    REBVAL *contexts;   // contexts to search for variables
    REBCNT default_cmd; // index of default_cmd, it's 1 if the object is selfless, 2 otherwise.
} REBDIA;

enum {
    RDIA_NO_CMD,        // do not store command in block
    RDIA_LIT_CMD,       // 'command
    RDIA_ALL,           // all commands, do not reset output
    RDIA_MAX
};

static REBINT Delect_Debug = 0;
static REBINT Total_Missed = 0;
static const char *Dia_Fmt = "DELECT - cmd: %s length: %d missed: %d total: %d";


//
//  Find_Mutable_In_Contexts: C
// 
// Search a block of objects for a given word symbol and
// return the value for the word. NULL if not found.
//
REBVAL *Find_Mutable_In_Contexts(REBSTR *sym, REBVAL *where)
{
    REBVAL *val;

    REBVAL safe;

    for (; NOT_END(where); where++) {
        if (IS_WORD(where)) {
            val = GET_MUTABLE_VAR_MAY_FAIL(where, SPECIFIED);
        }
        else if (IS_PATH(where)) {
            if (Do_Path_Throws_Core(&safe, NULL, where, SPECIFIED, NULL))
                fail (Error_No_Catch_For_Throw(&safe));
            val = &safe;
        }
        else
            val = where;

        if (IS_OBJECT(val)) {
            val = Select_Canon_In_Context(VAL_CONTEXT(val), STR_CANON(sym));
            if (val) return val;
        }
    }
    return 0;
}


//
//  Find_Command: C
// 
// Given a word, check to see if it is in the dialect object.
// If so, return its index. If not, return 0.
//
static int Find_Command(REBCTX *dialect, REBVAL *word)
{
    REBINT n;

    if (IS_WORD_BOUND(word) && dialect == VAL_WORD_CONTEXT(word))
        n = VAL_WORD_INDEX(word);
    else {
        n = Find_Canon_In_Context(dialect, VAL_WORD_CANON(word), FALSE);
        if (n != 0) {
            CLEAR_VAL_FLAG(word, VALUE_FLAG_RELATIVE);
            SET_VAL_FLAG(word, WORD_FLAG_BOUND);
            INIT_WORD_CONTEXT(word, dialect);
            INIT_WORD_INDEX(word, n);
        }
        else return 0;
    }

    // If keyword (not command) return negated index:
    if (IS_BLANK(CTX_VAR(dialect, n))) return -n;
    return n;
}


//
//  Count_Dia_Args: C
// 
// Return number of formal args provided to the function.
// This is just a guess, because * repeats count as zero.
//
static int Count_Dia_Args(REBVAL *args)
{
    REBINT n = 0;

    for (; NOT_END(args); args++) {
        if (IS_WORD(args)) {
            if (VAL_WORD_SYM(args) == SYM_ASTERISK) { // skip: * type
                if (NOT_END(args+1)) args++;
            } else n++;
        }
        else if (IS_DATATYPE(args) || IS_TYPESET(args)) n++;
    }
    return n;
}


//
//  Eval_Arg: C
// 
// Handle all values passed in a dialect.
// 
// Contexts can be used for finding a word in a block of
// contexts without using a path.
// 
// Returns zero on error.
// Note: stack used to hold temp values
//
static REBVAL *Eval_Arg(REBDIA *dia)
{
    REBVAL *value = KNOWN(ARR_AT(dia->args, dia->argi));

    REBVAL safe;

    switch (VAL_TYPE(value)) {

    case REB_WORD:
        // Only look it up if not part of dialect:
        if (Find_Command(dia->dialect, value) == 0) {
            REBVAL *val = value; // save
            if (dia->contexts) {
                value = Find_Mutable_In_Contexts(
                    VAL_WORD_CANON(value), dia->contexts
                );
                if (value) break;
            }

            // value comes back NULL if protected or not found
            //
            value = TRY_GET_MUTABLE_VAR(val, GUESSED);
        }
        break;

    case REB_PATH:
        if (Do_Path_Throws_Core(&safe, NULL, value, GUESSED, NULL))
            fail (Error_No_Catch_For_Throw(&safe));
        if (IS_FUNCTION(&safe)) return NULL;
        DS_PUSH(&safe);
        value = DS_TOP;
        break;

    case REB_LIT_WORD:
        DS_PUSH(value);
        value = DS_TOP;
        VAL_SET_TYPE_BITS(value, REB_WORD); // don't reset header - keeps binding
        break;

    case REB_GROUP:
        if (DO_VAL_ARRAY_AT_THROWS(&safe, value)) {
            // !!! Does not check for thrown cases...what should this
            // do in case of THROW, BREAK, QUIT?
            fail (Error_No_Catch_For_Throw(&safe));
        }
        DS_PUSH(&safe);
        value = DS_TOP;
        break;
    }

    return value;
}


//
//  Add_Arg: C
// 
// Add an actual argument to the output block.
// 
// Note that the argument may be out sequence with the formal
// arguments so we must scan for a slot that matches.
// 
// Returns:
//   1: arg matches a formal arg and has been stored
//   0: no arg of that type was found
//  -N: error (type block contains a bad value)
//
static REBINT Add_Arg(REBDIA *dia, REBVAL *value)
{
    REBINT type = 0;
    REBINT accept = 0;
    REBVAL *fargs;
    REBINT fargi;
    REBVAL *outp;
    REBINT rept = 0;

    outp = KNOWN(ARR_AT(dia->out, dia->outi));

    // Scan all formal args, looking for one that matches given value:
    for (fargi = dia->fargi;; fargi++) {

        //Debug_Fmt("Add_Arg fargi: %d outi: %d", fargi, outi);

        if (IS_END(fargs = KNOWN(ARR_AT(dia->fargs, fargi)))) return 0;

again:
        // Formal arg can be a word (type or refinement), datatype, or * (repeater):
        if (IS_WORD(fargs)) {

            // If word is a datatype name:
            type = VAL_WORD_SYM(fargs);
            if (type < REB_MAX) {
                type--; // the type id
            }
            else if (type == SYM_ASTERISK) {
                // repeat: * integer!
                rept = 1;
                fargs++;
                goto again;
            }
            else {
                // typeset or refinement
                REBVAL *temp;

                type = -1;

                // Is it a refinement word?
                if (IS_WORD(value) && VAL_WORD_CANON(fargs) == VAL_WORD_CANON(value)) {
                    accept = 4;
                }
                // Is it a typeset?
                else if (
                    (temp = TRY_GET_MUTABLE_VAR(fargs, GUESSED))
                    && IS_TYPESET(temp)
                ) {
                    if (TYPE_CHECK(temp, VAL_TYPE(value))) accept = 1;
                }
                else if (!IS_WORD(value)) return 0; // do not search past a refinement
                //else return -REB_DIALECT_BAD_SPEC;
            }
        }
        // It's been reduced and is an actual datatype or typeset:
        else if (IS_DATATYPE(fargs)) {
            type = VAL_TYPE_KIND(fargs);
        }
        else if (IS_TYPESET(fargs)) {
            if (TYPE_CHECK(fargs, VAL_TYPE(value))) accept = 1;
        } else
            return -REB_DIALECT_BAD_SPEC;

        // Make room for it in the output block:
        if (IS_END(outp)) {
            outp = Alloc_Tail_Array(dia->out);
            SET_BLANK(outp);
        } else if (!IS_BLANK(outp)) {
            // There's already an arg in this slot, so skip it...
            if (dia->cmd > dia->default_cmd) outp++;
            if (!rept) continue; // see if there's another farg that will work for it
            // Look for first empty slot:
            while (NOT_END(outp) && !IS_BLANK(outp)) outp++;
            if (IS_END(outp)) {
                outp = Alloc_Tail_Array(dia->out);
                SET_BLANK(outp);
            }
        }

        // The datatype was correct from above!
        if (accept) break;

        //Debug_Fmt("want: %d got: %d rept: %d", type, VAL_TYPE(value), rept);

        // Direct match to datatype or to integer/decimal coersions:
        if (type == (REBINT)VAL_TYPE(value)) {
            accept = 1;
            break;
        }
        else if (type == REB_INTEGER && IS_DECIMAL(value)) {
            accept = 2;
            break;
        }
        else if (type == REB_DECIMAL && IS_INTEGER(value)) {
            accept = 3;
            break;
        }

        dia->missed++;              // for debugging

        // Repeat did not match, so stop repeating and remove unused output slot:
        if (rept) {
            Remove_Array_Last(dia->out);
            outp--;
            rept = 0;
            continue;
        }

        if (dia->cmd > 1) outp++;   // skip output slot (for non-default values)
    }

    // Process the result:
    switch (accept) {

    case 1:
        *outp = *value;
        break;

    case 2:
        SET_INTEGER(outp, (REBI64)VAL_DECIMAL(value));
        break;

    case 3:
        SET_DECIMAL(outp, (REBDEC)VAL_INT64(value));
        break;

    case 4: // refinement:
        dia->fargi = fargs - KNOWN(ARR_HEAD(dia->fargs)) + 1;
        dia->outi = outp - KNOWN(ARR_HEAD(dia->out)) + 1;
        *outp = *value;
        return 1;

    case 0:
        return 0;
    }

    // Optimization: arg was in correct order:
    if (!rept && fargi == (signed)(dia->fargi)) {
        dia->fargi++;
        dia->outi++;
    }

    return 1;
}


//
//  Do_Cmd: C
// 
// Returns the length of command processed or error. See below.
//
static REBINT Do_Cmd(REBDIA *dia)
{
    REBVAL *fargs;
    REBINT size;
    REBVAL *val;
    REBINT err;
    REBINT n;
    REBSER *ser;

    // Get formal arguments block for this command:
    fargs = CTX_VAR(dia->dialect, dia->cmd);
    if (!IS_BLOCK(fargs)) return -REB_DIALECT_BAD_SPEC;
    dia->fargs = VAL_ARRAY(fargs);
    fargs = KNOWN(VAL_ARRAY_AT(fargs));
    size = Count_Dia_Args(fargs); // approximate

    ser = ARR_SERIES(dia->out);
    // Preallocate output block (optimize for large blocks):
    if (dia->len > size) size = dia->len;
    if (GET_FLAG(dia->flags, RDIA_ALL)) {
        Extend_Series(ser, size + 1);
    }
    else {
        Resize_Series(ser, size + 1); // tail = 0
    }

    // Insert command word:
    if (!GET_FLAG(dia->flags, RDIA_NO_CMD)) {
        val = Alloc_Tail_Array(dia->out);
        Val_Init_Word_Bound(
            val,
            GET_FLAG(dia->flags, RDIA_LIT_CMD) ? REB_LIT_WORD : REB_WORD,
            CTX_KEY_CASED(dia->dialect, dia->cmd),
            dia->dialect,
            dia->cmd
        );
        dia->outi++;
        size++;
    }
    if (dia->cmd > dia->default_cmd) dia->argi++; // default cmd has no word arg

    // Foreach argument provided:
    for (n = dia->len; n > 0; n--, dia->argi++) {
        val = Eval_Arg(dia);
        if (!val)
            return -REB_DIALECT_BAD_ARG;
        if (IS_END(val)) break;
        if (!IS_BLANK(val)) {
            //Print("n %d len %d argi %d", n, dia->len, dia->argi);
            err = Add_Arg(dia, val); // 1: good, 0: no-type, -N: error
            if (err == 0) return n; // remainder
            if (err < 0) return err;
        }
    }

    // If not enough args, pad with NONE values:
    if (dia->cmd > dia->default_cmd) {
        for (n = ARR_LEN(dia->out); n < size; n++) {
            REBVAL *temp = Alloc_Tail_Array(dia->out);
            SET_BLANK(temp);
        }
    }

    dia->outi = ARR_LEN(dia->out);

    return 0;
}


//
//  Do_Dia: C
// 
// Process the next command in the dialect.
// Returns the length of command processed.
// Zero indicates end of block.
// Negative indicate error.
// The args holds resulting args.
//
static REBINT Do_Dia(REBDIA *dia)
{
    REBVAL *next = KNOWN(ARR_AT(dia->args, dia->argi));
    REBVAL *head;
    REBINT err;

    if (IS_END(next)) return 0;

    // Find the command if a word is provided:
    if (IS_WORD(next) || IS_LIT_WORD(next)) {
        if (IS_LIT_WORD(next)) SET_FLAG(dia->flags, RDIA_LIT_CMD);
        dia->cmd = Find_Command(dia->dialect, next);
    }

    // Handle defaults - process values before a command is reached:
    if (dia->cmd <= dia->default_cmd) {
        dia->cmd = dia->default_cmd;
        dia->len = 1;
        err = Do_Cmd(dia); // DEFAULT cmd
        // It must be processed, else it is not in the dialect.
        // Check for noop result:
        if (err > 0) err = -REB_DIALECT_BAD_ARG;
        return err;
    }

    // Delimit the command - search for next command or end:
    for (head = ++next; NOT_END(next); next++) {
        if ((IS_WORD(next) || IS_LIT_WORD(next)) && Find_Command(dia->dialect, next) > 1) break;
    }

    // Note: command may be shorter than length provided here (defaults):
    dia->len = next - head; // length of args, not including command
    err = Do_Cmd(dia);
    if (GET_FLAG(dia->flags, RDIA_LIT_CMD)) dia->cmd += DIALECT_LIT_CMD;
    return err;
}


//
//  Do_Dialect: C
// 
// Format for dialect is:
//     CMD arg1 arg2 arg3 CMD arg1 arg2 ...
// 
// Returns:
//     cmd value or error as result (or zero for end)
//     index is updated
//     if *out is zero, then we create a new output block
// 
// The arg sequence is terminated by:
//     1. Maximum # of args for command
//     2. An arg that is not of a specified datatype for CMD
//     3. Encountering a new CMD
//     4. End of the dialect block
//
REBINT Do_Dialect(REBCTX *dialect, REBARR *block, REBCNT *index, REBARR **out)
{
    REBDIA dia;
    REBINT n;
    REBDSP dsp_orig = DSP; // Save stack position
    REBCNT self_index;

    CLEARS(&dia);

    if (*index >= ARR_LEN(block)) return 0; // end of block

    // !!! This used to say "Avoid GC during Dialect (prevents unknown
    // crash problem)".  To the extent DELECT is still used, this unknown
    // crash problem should be resolved...not the GC disabled.

    if (!*out) *out = Make_Array(25);

    dia.dialect = dialect;
    dia.args = block;
    dia.argi = *index;
    dia.out  = *out;
    SET_FLAG(dia.flags, RDIA_NO_CMD);

    self_index = Find_Canon_In_Context(dialect, Canon(SYM_SELF), TRUE);
    dia.default_cmd = self_index == 0 ? 1 : SELFISH(1);

    //Print("DSP: %d Dinp: %r - %m", DSP, ARR_AT(block, *index), block);
    n = Do_Dia(&dia);
    //Print("DSP: %d Dout: CMD: %d %m", DSP, dia.cmd, *out);
    DS_DROP_TO(dsp_orig); // pop any temp values used above

    if (Delect_Debug > 0) {
        Total_Missed += dia.missed;
        // !!!! debug
        if (dia.missed) {
            Debug_Fmt(
                Dia_Fmt,
                STR_HEAD(CTX_KEY_CASED(dia.dialect, dia.cmd)),
                ARR_LEN(dia.out),
                dia.missed,
                Total_Missed
            );
        }
    }

    if (n < 0) return n; //error
    *index = dia.argi;

    return dia.cmd;
}


//
//  delect: native [
//  
//  {Parses a common form of dialects. Returns updated input block.}
//  
//      dialect [object!]
//          "Describes the words and datatypes of the dialect"
//      input [block!]
//          "Input stream to parse"
//      output [block!]
//          "Resulting values, ordered as defined (modified)"
//      /in
//          {Search for var words in specific objects (contexts)}
//      where [block!]
//          "Block of objects to search (non objects ignored)"
//      /all
//          "Parse entire block, not just one command at a time"
//  ]
//
REBNATIVE(delect)
{
    PARAM(1, dialect);
    PARAM(2, input);
    PARAM(3, output);
    REFINE(4, in);
    PARAM(5, where);
    REFINE(6, all);

    REBDIA dia;
    REBINT err;
    REBDSP dsp_orig;
    REBCNT self_index;

    CLEARS(&dia);

    dia.dialect = VAL_CONTEXT(ARG(dialect));
    dia.args = VAL_ARRAY(ARG(input));
    dia.argi = VAL_INDEX(ARG(input));
    dia.out = VAL_ARRAY(ARG(output));
    dia.outi = VAL_INDEX(ARG(output));

    if (dia.argi >= ARR_LEN(dia.args)) return R_BLANK; // end of block

    self_index = Find_Canon_In_Context(dia.dialect, Canon(SYM_SELF), TRUE);
    dia.default_cmd = self_index == 0 ? 1 : SELFISH(1);

    if (REF(in)) {
        dia.contexts = ARG(where);
        if (!IS_BLOCK(dia.contexts))
            fail (Error_Invalid_Arg(dia.contexts));
        dia.contexts = KNOWN(VAL_ARRAY_AT(dia.contexts));
    }

    dsp_orig = DSP;

    if (REF(all)) {
        SET_FLAG(dia.flags, RDIA_ALL);
        Resize_Series(ARR_SERIES(dia.out), VAL_LEN_AT(ARG(input)));
        while (TRUE) {
            dia.cmd = 0;
            dia.len = 0;
            dia.fargi = 0;
            err = Do_Dia(&dia);
            if (err < 0 || IS_END(ARR_AT(dia.args, dia.argi))) break;
        }
    }
    else
        err = Do_Dia(&dia);

    DS_DROP_TO(dsp_orig);

    VAL_INDEX(ARG(input)) = MIN(dia.argi, ARR_LEN(dia.args));

    if (Delect_Debug > 0) {
        Total_Missed += dia.missed;
        if (dia.missed) {
            Debug_Fmt(
                Dia_Fmt,
                STR_HEAD(CTX_KEY_CASED(dia.dialect, dia.cmd)),
                ARR_LEN(dia.out),
                dia.missed,
                Total_Missed
            );
        }
    }

    if (err < 0) fail (Error_Invalid_Arg(ARG(input))); // !!! make better error

    *D_OUT = *ARG(input);
    return R_OUT;
}


//
//  Trace_Delect: C
//
void Trace_Delect(REBINT level)
{
    Delect_Debug = level;
}

//
//  File: %mod-secure.c
//  Summary: "SECURE extension"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See notes in %extensions/secure/README.md

#include "sys-core.h"

#include "tmp-mod-secure.h"


// R3-Alpha's SECURE dialect identified the security options by SYM_XXX
// symbol values, but turned them into smaller integers to compact them into
// bit flags.  Those bit flags were passed into the "security API" in R3-Alpha
// but they are quarantined into this extension as an implementation detail.

enum Reb_Security_Flags {
    SEC_ALLOW,
    SEC_ASK,
    SEC_THROW,
    SEC_QUIT,
    SEC_MAX
};

enum Reb_Security_Byte_Offsets {
    POL_READ,
    POL_WRITE,
    POL_EXEC,
    POL_MAX
};


//
//  Match_Sub_Path: C
//
// Compare two file path series and return true if s1 is a subpath of s2.
// Case insensitive.
//
// !!! This C code would likely be better as Rebol in %ext-secure-init.reb
//
bool Match_Sub_Path(const REBSTR *s1, const REBSTR *s2)
{
    REBLEN len1 = STR_LEN(s1);
    if (len1 > STR_LEN(s2))
        return false;

    REBCHR(const*) cp1 = STR_HEAD(s1);
    REBCHR(const*) cp2 = STR_HEAD(s2);
    REBUNI c1 = 0;
    REBUNI c2;

    REBLEN n;
    for (n = 0; n != len1; n++) {  // includes terminator
        cp1 = NEXT_CHR(&c1, cp1);
        cp2 = NEXT_CHR(&c2, cp2);
        if (LO_CASE(c1) != LO_CASE(c2))
            return false;  // all chars must match
    }

    // a/b matches: a/b, a/b/, a/b/c
    //
    cp2 = NEXT_CHR(&c2, cp2);
    return did (
            n >= len1 // all chars matched
            and  // Must be at end or at dir sep:
            (c1 == '/' or c1 == '\\'
            or c2 == 0 or c2 == '/' or c2 == '\\')
    );
}


//
//  Security_Policy: C
//
// Given a security symbol (like FILE) and a value (like the file path)
// returns the security policy (Read/Write/eXecute) allowed for it.  Returns
// unaligned byte array of flags for the policy class:
//
//     flags: [rrrr wwww xxxx ----]
//
//     Where each byte is:
//         0: SEC_ALLOW
//         1: SEC_ASK
//         2: SEC_THROW
//         3: SEC_QUIT
//
// The secuity is defined by the system/state/policies object, that
// is of the form:
//
//     [
//         file:  [%file1 tuple-flags %file2 ... default tuple-flags]
//         net:   [...]
//         call:  tuple-flags
//         stack: tuple-flags
//         eval:  integer (limit)
//     ]
//
const REBYTE *Security_Policy(
    REBSTR *subsystem,  // word that represents the type ['file 'net]
    const REBVAL *name  // file or path value to check security of
){
    const REBVAL *policies = Get_System(SYS_STATE, STATE_POLICIES);
    if (not IS_OBJECT(policies))
        fail (policies);

    const REBVAL *policy = Select_Canon_In_Context(
        VAL_CONTEXT(policies),
        STR_CANON(subsystem)
    );
    if (not policy) {
        DECLARE_LOCAL (word);
        Init_Word(word, subsystem);
        fail (Error(SYM_SECURITY, SYM_SECURITY_ERROR, word));
    }

    if (IS_TUPLE(policy))  // just a tuple (e.g. [file rrrr.wwww.xxxx])
        fail ("This returned VAL_TUPLE(policy)...review");

    if (not IS_BLOCK(policy))  // only other form is detailed block
        fail (policy);

    // Scan block of policies for the class: [file [allow read quit write]]

    REBLEN len = 0;  // file or url length
    const REBYTE *flags = 0;  // policy flags

    // !!! Comment said "no relatives in STATE_POLICIES"
    //
    const RELVAL *item = ARR_HEAD(VAL_ARRAY(policy));

    for (; NOT_END(item); item += 2) {
        if (IS_END(item + 1) or not IS_TUPLE(item + 1))  // must map to tuple
            fail (policy);

        if (IS_WORD(item)) { // !!! Comment said "any word works here"
            if (len == 0)  // !!! "If no strings found, use the default"
                fail ("This set flags = VAL_TUPLE(item + 1), review");
        }
        else if (name and (IS_TEXT(item) or IS_FILE(item))) {
            //
            // !!! Review doing with usermode code in %ext-secure-init.reb
            //
            if (Match_Sub_Path(VAL_STRING(item), VAL_STRING(name))) {
                if (VAL_LEN_HEAD(name) >= len) {  // "Is the match adequate?"
                    len = VAL_LEN_HEAD(name);
                    fail ("This set flags = VAL_TUPLE(item + 1), review");
                }
            }
        }
        else
            fail (policy);
    }

    if (flags == 0)
        fail (policy);

    return flags;
}


//
//  Trap_Security: C
//
// Take action on the policy flags provided. The sym and value
// are provided for error message purposes only.
//
void Trap_Security(REBLEN flag, REBSTR *subsystem, const REBVAL *value)
{
    if (flag == SEC_THROW) {
        if (value == nullptr) {
            Init_Word(DS_PUSH(), subsystem);
            value = DS_TOP;
        }
        fail (Error_Security_Raw(value));
    }
    else if (flag == SEC_QUIT)
        exit(101);
}


//
//  Check_Security: C
//
// A helper function that fetches the security flags for a given symbol (FILE)
// and value (path), and then tests that they are allowed.
//
// !!! To keep this stub routine from being included by things like the
// JavaScript extension, the places that used to call it instead call the
// `Check_Security_Placeholder()` function.  A real solution would be done
// via a Rebol routine or HIJACK, which would permit dynamic linking.
//
void Check_Security(
    REBSTR *subsystem,  // e.g. FILE, DEBUG, MEMORY, CALL
    enum Reb_Symbol policy,  // e.g. READ, WRITE, EXEC
    const REBVAL *value  // e.g. the file path being read/written
){
    REBLEN pol;
    switch (policy) {
      case SYM_READ:
        pol = POL_READ;
        break;

      case SYM_WRITE:
        pol = POL_WRITE;
        break;

      case SYM_EXEC:
        pol = POL_EXEC;
        break;

      default:
        fail ("Invalid security policy in Check_Security_Placeholder()");
    }

    const REBYTE *flags = Security_Policy(subsystem, value);
    Trap_Security(flags[pol], subsystem, value);
}


//
//  init-secure: native [
//
//  {Initialize the SECURE Extension}
//
//  ]
//
REBNATIVE(init_secure)
//
// !!! Technically there's nothing SECURE needs for initialization, but all
// extensions currently must have at least one native.  Review that rule.
{
    SECURE_INCLUDE_PARAMS_OF_INIT_SECURE;

    return Init_Void(D_OUT);
}

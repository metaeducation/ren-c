//
//  file: %needful-loops.h
//  summary: "Useful looping constructs matching ATTEMPT and UNTIL in Ren-C"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// This is a fun trick that brings a little bit of the ATTEMPT and UNTIL loop
// functionality from Ren-C into C.
//
// The `attempt` macro is a loop that runs its body just once, and then
// evaluates the `then` or `else` clause (if present):
//
//     attempt {
//         ... some code ...
//         if (condition) { break; }  /* exit attempt, run "else" clause */
//         if (condition) { continue; }  /* exit attempt, run "then" clause */
//         if (condition) { again; }  /* jump to attempt and run it again */
//         ... more code ...
//     }
//     then {  /* optional then clause */
//        ... code to run if no break happened ...
//     }
//     else {  /* optional else clause (must have then clause to use else) */
//        ... code to run if a break happened ...
//     }
//
// It doesn't do anything you couldn't do with defining some goto labels.
// But if you have B breaks and C continues and A agains, you don't have to
// type the label names ((B + 1) + (C + 1) + (A + 1)) times.  And you don't
// have to worry about coming up with the names for those labels!
//
// The `until` macro is a negated sense while loop that also is able to have
// compatibility with the `then` and `else` clauses.
//
// BUT NOTE: Since the macros define variables tracking whether the `then`
// clause should run or not, and whether an `again` should signal continuing
// to run...this can only be used in one scope at a time.  To use more than
// once in a function, define another scope.  Also, you can't use an `else`
// clause without a `then` clause.

#define attempt \
    bool run_then_ = false;  /* as long as run_then_ is false, keep going */ \
    bool run_again_ = false;  /* if run_again_, don't set run_then_ */ \
    for (; not run_then_; \
        run_again_ ? (run_again_ = false), true  /* again doesn't exit loop */ \
        : (run_then_ = true))  /* normal continue, exits the loop */

#define until(condition) \
    bool run_then_ = false; \
    bool run_again_ = false; \
    for (; run_again_ ? (run_again_ = false), true :  /* skip condition */ \
        (condition) ? (run_then_ = true, false) : true; )

#define then  if (run_then_)
#define again  { run_again_ = true; continue; }

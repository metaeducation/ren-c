REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Datatype definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        These words define the REBOL datatypes and their related attributes.
        This table generates a variety of C defines and intialization tables.
        During build, when this file is processed, this section is changed to
        hold just the datatype words - the initial entries the word table.

        name        - name of datatype (generates words)
        class       - how type actions are dispatched (T_type), + is extension
        path        - it supports various path forms (* for same as typeclass)
        make        - It can be made with #[datatype] method
        typesets    - what typesets the type belongs to

        Note that if there is `somename` in the class column, that means you
        will find the ACTION! dispatch for that type in `REBTYPE(Somename)`.

        If the (CLASS) is in a GROUP! that means it has evaluator behavior,
        vs. being passed through as-is.  (e.g. a lit-word is "evaluative")
        This is used to build the table used for fast lookup of whether the
        evaluator needs to be called on a given type.
    }
]


[name       class       path    make    typesets]

; 0 is not a real data type.  It is reserved for internal purposes.

0           0           -       -       -

; There is only one FUNCTION! type in Ren-C

function    function    *       *       -

; ANY-WORD!, order matters (tests like ANY_WORD use >= REB_WORD, <= REB_ISSUE)
;
word        word        -       *       word
set-word    word        -       *       word
get-word    word        -       *       word
lit-word    word        -       *       word
refinement  word        -       *       word
issue       word        -       *       word

; ANY-ARRAY!, order matters (and contiguous with ANY-SERIES below matters!)
;
path        array       *       *       [series path array]
set-path    array       *       *       [series path array]
get-path    array       *       *       [series path array]
lit-path    array       *       *       [series path array]
group       array       *       *       [series array]
block       array       *       *       [series array]

; ANY-SERIES!, order matters (and contiguous with ANY-ARRAY above matters!)
;
binary      string      *       *       [series]
string      string      *       *       [series string]
file        string      *       *       [series string]
email       string      *       *       [series string]
url         string      *       *       [series string]
tag         string      *       *       [series string]

bitset      bitset      *       *       -
image       image       *       *       [series]
vector      vector      *       *       [series]

map         map         *       *       -

varargs     varargs     *       *       -

object      context     *       *       context
frame       context     *       *       context
module      context     *       *       context
error       context     *       *       context
port        port        context *       context

; ^-------- Everything above is a "bindable" type, see Is_Bindable() --------^

; v------- Everything below is an "unbindable" type, see Is_Bindable() ------v

; "unit types" https://en.wikipedia.org/wiki/Unit_type

bar         unit        -       *       -
lit-bar     unit        -       *       -
blank       unit        -       *       -

; scalars

logic       logic       -       *       -
integer     integer     -       *       [number scalar]
decimal     decimal     -       *       [number scalar]
percent     decimal     -       *       [number scalar]
money       money       -       *       scalar
char        char        -       *       scalar
pair        pair        *       *       scalar
tuple       tuple       *       *       scalar
time        time        *       *       scalar
date        date        *       *       -

; type system

datatype    datatype    -       *       -
typeset     typeset     -       *       -

; things likely to become user-defined types or extensions

gob         gob         *       *       -
event       event       *       *       -
handle      handle      -       -       -
struct      struct      *       *       -
library     library     -       *       -

; Note that the "void?" state has no associated VOID! datatype.  Internally
; it uses REB_MAX, but like the REB_0 it stays off the type map.  (REB_0
; is used for lookback as opposed to void in order to implement an
; optimization in Get_Var_Core())

REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Extension datatypes"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Used to build C enums and definitions for extensions.
    }
]

trash       *   0
end         1   0
unset       *   null
none        *   null
handle      *   ptr

logic       5   32
integer     *   64
decimal     *   64
percent     *   64

char        11  32
pair        *   64
tuple       *   64
time        *   64
date        *   date

word        17  sym
set-word    *   sym
get-word    *   sym
lit-word    *   sym
refinement  *   sym
issue       *   sym

string      25  ser
file        *   ser
email       *   ser
url         *   ser
tag         *   ser

block       33  ser
paren       *   ser
path        *   ser
set-path    *   ser
get-path    *   ser
lit-path    *   ser

binary      41  ser
bitset      *   ser
vector      *   ser
image       *   image

gob         48  ser

object      49  ptr
module      *   ptr


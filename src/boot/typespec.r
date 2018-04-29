REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Datatype help spec"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Provides useful information about datatypes.
        Can be expanded to include info like min-max ranges.
    }
]

action      ["an invokable Rebol subroutine" action]
bar         ["expression evaluation barrier" internal]
binary      ["string series of bytes" string]
bitset      ["set of bit flags" string]
blank       ["placeholder unit type which also is conditionally FALSE?" scalar]
block       ["array of values that blocks evaluation unless DO is used" block]
char        ["8bit and 16bit character" scalar]
datatype    ["type of datatype" symbol]
date        ["day, month, year, time of day, and timezone" scalar]
decimal     ["64bit floating point number (IEEE standard)" scalar]
email       ["email address" string]
error       ["errors and throws" context]
event       ["user interface event (efficiently sized)" opt-object]
file        ["file name or path" string]
frame       ["arguments and locals of a specific action invocation" context]
get-path    ["the value of a path" block]
get-word    ["the value of a word (variable)" word]
gob         ["graphical object" opt-object]
handle      ["arbitrary internal object or value" internal]
image       ["RGB image with alpha channel" vector]
integer     ["64 bit integer" scalar]
issue       ["identifying marker word" word]
library     ["external library reference" internal]
lit-bar     ["literal expression barrier" internal]
lit-path    ["literal path value" block]
lit-word    ["literal word value" word]
logic       ["boolean true or false" scalar]
map         ["name-value pairs (hash associative)" block]
module      ["loadable context of code and data" context]
money       ["high precision decimals with denomination (opt)" scalar]
object      ["context of names with values" context]
pair        ["two dimensional point or size" scalar]
group       ["array that evaluates expressions as an isolated group" block]
path        ["refinements to functions, objects, files" block]
percent     ["special form of decimals (used mainly for layout)" scalar]
port        ["external series, an I/O channel" context]
refinement  ["variation of meaning or location" word]
set-path    ["definition of a path's value" block]
set-word    ["definition of a word's value" word]
string      ["string series of characters" string]
struct      ["native structure definition" block]
tag         ["markup string (HTML or XML)" string]
time        ["time of day or duration" scalar]
tuple       ["sequence of small integers (colors, versions, IP)" scalar]
typeset     ["set of datatypes" opt-object]
unicode     ["string of unicoded characters" string]
url         ["uniform resource locator or identifier" string]
varargs     ["evaluator position for variable numbers of arguments" internal]
vector      ["high performance arrays (single datatype)" vector]
word        ["word (symbol or variable)" word]

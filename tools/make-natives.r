REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate native specifications"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2020 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        "Natives" are Rebol functions whose implementations are C code (as
        opposed to blocks of user code, such as that made with FUNC).
        
        Though their bodies are C, native specifications are Rebol blocks.
        For convenience that Rebol code is kept in the same file as the C
        definition--in a C comment block.

        Typically the declarations wind up looking like this:

        //
        //  native-name: native [
        //
        //  {Description of native would go here}
        //
        //      return: "Return description here"
        //          [integer!]
        //      argument "Argument description here"
        //          [text!]
        //      /refinement "Refinement description here"
        //  ]
        //
        REBNATIVE(native_name) {
            INCLUDE_PARAMS_OF_NATIVE_NAME;

            if (REF(refinement)) {
                 int i = VAL_INT32(argument);
                 /* etc, etc. */
            }
            return D_OUT;
        }

        (Note that the C name of the native may need to be different from the
        Rebol native; e.g. above the `-` cannot be part of a name in C, so
        it gets converted to `_`.  See TO-C-NAME for the logic of this.)

        In order for these specification blocks to be loaded along with the
        function when the interpreter is built, a step in the build has to
        scan through the files to scrape them out of the comments.  This
        file implements that step, which recursively searches %src/core for
        any `.c` files.  Similar code exists for processing "extensions",
        which also implement C functions for natives.

        Not only does the text for the spec have to be extracted, but the
        `INCLUDE_PARAMS_OF_XXX` macros are generated to give a more readable
        way of accessing the parameters than by numeric index.  See the
        REF() and ARG() macro definitions for more on this.
    }
]

do %common.r
do %common-parsers.r
do %native-emitters.r ;for emit-native-proto

print "------ Generate tmp-natives.r"

src-dir: %../src/
output-dir: make-file [(system/options/path) prep /]
mkdir/deep make-file [(output-dir) boot /]

verbose: false

unsorted-buffer: make text! 20000

process: function [
    file
    <with> the-file
][
    the-file: file
    if verbose [probe [file]]

    source-text: read/string file
    proto-parser/emit-proto: :emit-native-proto
    proto-parser/process source-text
]

;-------------------------------------------------------------------------

output-buffer: make text! 20000


proto-count: 0

gather-natives: func [dir] [
    files: read dir
    for-each file files [
        file: join dir file
        case [
            dir? file [gather-natives file]
            all [
                %.c = suffix? file
            ][
                process file
            ]
        ]
    ]
]

gather-natives make-file [(src-dir) core /]


append output-buffer unsorted-buffer

write-if-changed make-file [(output-dir) boot/tmp-natives.r] output-buffer

print [proto-count "natives"]
print newline


print "------ Generate tmp-generics.r"

clear output-buffer

append output-buffer {REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Action function specs"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {This is a generated file.}
]

}

boot-types: load src-dir/boot/types.r

append output-buffer mold/only load src-dir/boot/generics.r

append output-buffer unspaced [
    newline
    "_  ; C code expects BLANK! evaluation result, at present" newline
    newline
]

write-if-changed make-file [(output-dir) boot/tmp-generics.r] output-buffer

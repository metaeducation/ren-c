
; Unassigned Trellos:
;
; https://trello.com/c/VRalC5VO/27-reflection-via-positional-pick-on-object-or-function-replaced-by-words-of-spec-of-body-of
; https://trello.com/c/gL9rHcEC/136-endable-args-replace-the-unset-quoting-hack-used-to-make-help-work-with-1-or-0-args-allows-infix-functions-to-implement-handling
; https://trello.com/c/cX0h3mve/167-infix-enfixed-functions-are-allowed-to-quote-their-left-hand-arguments
; https://trello.com/c/V3BVGAax/138-lambdas-for-defining-functions-such-as-f-x---print-x
; https://trello.com/c/honUZJ8C/134-you-can-specialize-functions-so-some-or-all-of-their-refinements-or-arguments-are-pre-filled
; https://trello.com/c/9sOtDYcS/86-failed-conditionals-like-if-false-or-while-false-return-no-value-at-all-instead-of-none
; https://trello.com/c/46zJ9lZj/94-passing-no-value-to-all-arguments-for-a-refinement-will-un-ask-for-the-refinement-having-them-part-valued-and-part-void-for-one
; https://trello.com/c/jMKxZrr9/154-print-requires-eval-to-process-expressions-that-evaluate-to-blocks-literal-blocks-in-source-do-not-need-this
; https://trello.com/c/AMR5aDOm/107-if-unless-and-either-accept-non-blocks-for-their-branches-only-refinement-for-literal-blocks-instead-of-evaluating-them
; https://trello.com/c/4zoWOf2H/71-while-handles-break-or-continue-in-its-body-block-only
; https://trello.com/c/eogcGhLX/72-map-each-supports-continue-and-break
; https://trello.com/c/6vCygf6C/151-case-follows-stricter-structure-must-pair-etc
; https://trello.com/c/fjJb3eR2/1-type-type-of
; https://trello.com/c/BbQNdTe1/127-context-of-replaces-bind-and-bound
; https://trello.com/c/DVXmdtIb/5-index-index-of-offset-offset-of-sign-sign-of-encoding-encoding-of
; https://trello.com/c/d0Nw87kp/75-any-prefix-for-series-number-scalar-type-synonyms-latter-preferred
; https://trello.com/c/q6pNyE82/56-to-integer-is-an-error-and-no-longer-0
; https://trello.com/c/bWlkV0M5/22-adding-char-to-a-binary-may-add-more-than-one-byte-utf-8-encoding-of-its-codepoint-so-use-integers
; https://trello.com/c/h5sq7af8/11-hash-not-supported-better-substitute-needed
; https://trello.com/c/ngldmNrW/15-wildcard-support-via-find-any-and-delete-any-removed-better-substitute-needed
; https://trello.com/c/yhNtrP5c/54-quit-returns-no-value-by-default-to-calling-script
; https://trello.com/c/Fv8053U6/55-catch-without-name-only-catches-throws-that-didnt-use-throw-name
; https://trello.com/c/3hCNux3z/60-quit-return-quit-with
; https://trello.com/c/Y9LTVGon/12-read-now-defaults-to-binary
; https://trello.com/c/IuGUJ9b9/13-read-string-expects-utf8-input-and-not-latin1-may-appear-to-not-handle-accented-characters
; https://trello.com/c/ZSvyZkHi/24-get-modes-using-full-path-replaced-by-clean-path
; https://trello.com/c/KvUZ5zEb/104-set-operations-union-exclude-etc-can-work-with-any-pair-of-any-string-or-any-array-types
; https://trello.com/c/coLzXVju/106-objects-can-have-optional-spec

; some old bug fixes that need grouping
"8baf6ca" [
    related: [
        "19d1111" "2b52014" "2b2ee73" "d82b061" "24836e6" "d566711" "0ea6b08"
        "32a6be5" "740a8c1" "42653da" "8a0ffb9" "c5c40d2"
    ]
    cc: [1989 1969 1957 1958 1491]
    summary: {Multiple bug fixes from Carl, Earl & Ladislav}
]

; some grouping of CC fixes
"9cd51ab" [
    related: ["ac9176a" "be2bd43" "82011e2"]
    cc: [1748 2068]
    summary: {Fix CC-2068 CC-1748}
]

; at moment entered manually but lets try and make use of related: in above for this
;"ac9176a" no
;"be2bd43" no
;"82011e2" no

{c94cc95} [trello: https://trello.com/c/m94GOELw/]  ; {Expand possible /NAME types for THROW}]

{fc7c536} [
    type: 'Added
    trello: https://trello.com/c/eaumDXoG/
] ; {Initial experimental HIJACK implementation}]


{df1a02a} [
    example: {
fail "Simple error"
}
    type: 'Added 
    trello: https://trello.com/c/tnRPtrx0/
] ; {Add FAIL native for raising an error or error-spec}]

"4c29aae" [type: 'Fixed] ;  Fixes size? modified? 

"58d6d5b" [type: 'Changed issues: [#508] example: {
>> a: 10 b: 20
>> set/some [a b] [99]
>> a
== 99
>> b
== 20
}] ;  * Make SET have /PAD semantics by default, add SET/SOME refinement 

"e263431" [type: 'Fixed]  ; Fix the home directory on Windows 
"fbe5237" [type: 'Changed]  ; Command-line option improvements (#508) 
"50e015f" [type: 'Changed]  ; Host-start --verbose changes (#507) 
"84b5214" [type: 'Added  wiki: https://github.com/r3n/reboldocs/wiki/User-and-Console
    summary: {Reinstated %user.reb start-up script (ala R2 %user.r) (#495)}
]  ; Added back %user.r start-up script (#495) 

"c1d6173" [type: 'Added]  ; Add /ONLY refinement to SET, permit single value to block sets 
"9aaa539" [type: 'Fixed]  ; Fix Ctrl-C handling in Windows console IO 
"96121b7" [type: 'Fixed]  ; Fix DNS reverse lookup when given a string 
"16aff4c" [type: 'Changed]  ; Make WHAT-DIR return copy of system variable 
"b86b5eb" [type: 'Changed]  ; Allow SET/PAD to be used with BLANK on BLOCK! targets 
"17f6a7e" [type: 'Fixed]  ; Fix encapped executable name on non-Windows 
"10b3f63" [type: 'Changed]  ; Unify semantics of GET-PATH + PICK*, and SET-PATH! + POKE 
"a17a144" [type: 'Changed]  ; Simplify legacy GET and SET for contexts 
"25a40c6" [type: 'Added]  ; Add just DETAB-FILE from pull request, with note, no line deletions 
"b1df8c0" [type: 'Changed]  ; Plain GET returns BLANK! for not set variables, GET-VALUE will error 
"e0ff829" [type: 'Changed]  ; Make GET-ENV return blank instead of void when variable not present 
"8ef2488" [type: 'Added]  ; Add an encapper for Windows PE 


;"00def28" [type: 'Changed]  ; REPL renamed to CONSOLE (#484) 
;"2955730" [type: 'Changed]  ; REPL - simple fixes and refactoring 
"922658f" [type: 'Added issues: [#475 #484] related: ["00def28" "2955730"]
    wiki: https://github.com/r3n/reboldocs/wiki/User-and-Console
    summary: "Added CONSOLE! object & skinning"
]  ; Added REPL object to allow skinning (#475) 

"d9114c8" [type: 'Changed]  ; Update MAKE DATE! and MAKE TIME! for BLOCK! input 
"62d137e" [type: 'Changed]  ; Make APPEND on port act as WRITE/APPEND in slightly less hacky way 
"29ae001" [type: 'Changed trello: https://trello.com/c/4OT7qvdu/]  ; LENGTH-OF, HEAD-OF, TAIL-OF as core names...aliased to shorter 
"523a890" [type: 'Changed]  ; Check for end of file newline for all text formats. (#471) 
"46cf8e7" [type: 'Added]  ; Introduce source checking of Rebol files. 
"080b9c6" [type: 'Changed]  ; Bring back DUMP changes, move DUMP out of base 
"b24f1ce" [type: 'Changed]  ; Allow hard-quoting of BAR!, yet still disallow QUOTE | 
"9232b3b" [type: 'Changed]  ; Disallow PICK on MAP! 
"92cbbcf" [type: 'Changed]  ; HELP patches to permit FOR-EACH [key val] lib [help :key] 

"33b8f02" [
    type: 'Added
    related: ["3f3aed1" "eaa5b28" "84da205"]
    trello: https://trello.com/c/pWMjGYl7/
]  ; voiding SELECT*, PICK*, TAKE* w/convenience wraps SELECT, PICK, TAKE 

"a65de09" [type: 'Fixed]  ; Fix HTTPS false alarm error 
"de46d25" [type: 'Added]  ; Customizable userspace ECHO based on HIJACK 
"c4e7d04" [type: 'Fixed]  ; Fix some definitional return and definitional leave bugs 

"177dfdc" [
    type: 'Added
    related: ["50430f1"]
    summary: {Added WITH_FFI to makefile and Win64 release build}
] ;  Add a WITH_FFI option to makefile 

"50430f1" no;  [type: 'Added]  ; Add Win64 release build 

"ef25751" [type: 'Changed]  ; Simplifying rewrite of REWORD, TO and THRU of BLANK! are no-ops 

"0e0ae9a" [
    type: 'Added
    related: ["47aef8a" "9fb973c" "0dff611"]
    trello: https://trello.com/c/IyxPieNa/
    summary: {<in>, <with>, <static> function specs}
]  ; <in>, <with>, <static> function specs...plus defaults 


"c1c5945" [
    example: {
if 1 = 1 [run-this] else [run-that]
}
    type: 'Added
    related: ["f3a5bf8" "3b3699d"]
    trello: https://trello.com/c/NPivtSdd/
    summary: {New ELSE & THEN enfix functions}
] ;  Add support for ELSE infix function generator 

"1e9621e" [type: 'Changed]  ; Check for END when using a DO rule in PARSE 
"8b6f1bf" [type: 'Added]  ; Add SHA256 hashing native to cryptography module 
"0abf2b6" [type: 'Fixed]  ; Fix an undefined behavior regarding to union access 

"4a89744" [
    type: 'Changed
    related: ["97ca06b" "4638e58" "36d9a74"]
    summary: {Turn BMP, JPG, GIF & PNG codec into an extension}
] ;  Turn BMP code into an extension 

"ef9975e" [type: 'Changed]  ; Support extensions as dynamically linked libraries 
"4ab24cd" [type: 'Changed]  ; Test @GrahamChiu's s3 upload for travis.yml 
"489ca6a" [type: 'Changed]  ; Put REPL and host startup code into lib, not user 
"185168f" [type: 'Changed]  ; Simpler/Better "Facade" (e.g. can use TIGHTEN on any function) 
"8769bb9" [type: 'Removed]  ; Get rid of DELECT 
"02de856" [type: 'Changed]  ; Crude draft of ADD and SUBTRACT on BINARY! 
"f97dfca" [type: 'Changed]  ; Use LODEPNG-based encoder for PNG, not %u-png.c version 
"f18a65f" [type: 'Changed trello: https://trello.com/c/Q3w0PzK3/]  ; Codecs as FUNCTION!s, can be C natives or custom usermode code 
"c7b68ea" [type: 'Changed]  ; Make seeks in PARSE via GET-WORD! reset `begin` between rules 
"c5c58cd" [type: 'Fixed]  ; Fix omitted result in legacy GET behavior 
"afaa179" [type: 'Added]  ; Add experimental DO-ALL native 
"9754fdb" [type: 'Fixed]  ; Fix () vs. (void) argument for make prep 
"8494e7a" [type: 'Changed]  ; Split REJOIN into JOIN-ALL and UNSPACED cases 
"5fda0a7" [type: 'Changed]  ; LOCK code which is not in modules, run from the command line 
"de0829c" [type: 'Changed]  ; Automatically add backtraces in error generating functions 
"8813896" [type: 'Fixed]  ; Fix error reporting in scripts run from command line 
"ce433ee" [type: 'Changed]  ; If arguments aren't evaluating, allow BAR! 
"401a96d" [type: 'Changed]  ; Make PIPE()-based CALL work asynchronously without PIPE2() 
"defc222" [type: 'Changed]  ; Implement the REPL's I/O as Rebol code 
"89f2202" [type: 'Fixed]  ; Fix SET-PATH! in PARSE 
"4e261b4" [type: 'Changed]  ; The only thing you can TO convert a blank into is a BLANK!, while TO BLANK! of any input is a blank. 
"38ce4ee" [type: 'Added]  ; Add RECYCLE/VERBOSE to Debug build 
"5a006d1" [type: 'Fixed]  ; Fixes so Ren-C can be used as R3-MAKE 
"efbaef9" [
    example: {
>> foo: func [x [integer! <variadic>]] [
        print ["foo takes" take x "and then" take x]
    ]

>> foo 1 2
foo takes 1 and then 2

>> foo 1 + 2 3 + 4
foo takes 3 and then 7
}
    type: 'Added 
    related: ["2aac4ed"] 
    trello: https://trello.com/c/Y17CEywN/
]  ; *experimental* VARARGS! - highly flexible variadics 

"26eb8ac" [type: 'Changed trello: https://trello.com/c/9ChhSWC4/]  ; Make SWITCH/DEFAULT handle non-block cases 
"32d3c4c" [type: 'Added]  ; Add return specification to LAST so it can return void/functions 
"fa21de3" [type: 'Changed]  ; Make crypt a module 
"c9fa8c0" [type: 'Changed]  ; Allow extensions to define their own errors 
"7a8c964" [type: 'Changed]  ; Reworked extension mechanism 
"f745cf9" [type: 'Fixed]  ; Fix RETURN in CHAIN, remove EXIT=QUIT, leaked series on QUIT 
"2f7b0b5" [type: 'Fixed]  ; Fix backward unicode encoding flag 
"b6b9a91" [type: 'Fixed]  ; Fix HELP on OBJECT! and PORT! values 
"d4bfbaf" [type: 'Changed]  ; REDUCE leaves BAR! in blocks, SET and GET tolerate BAR! 
"d1fd3ff" [type: 'Changed]  ; In-place Get of variables in contexts return relative values 
"da11022" [type: 'Changed trello: https://trello.com/c/DXs6gJNr/]  ; Initial Implementation of Immutable Source 
"5acc74f" [type: 'Fixed]  ; Fix system/catalog/* and make their initialization more clear 
"ae5fe8c" no ;[type: 'Changed]  ; Change LIB/USAGE call to just USAGE 
"1df7084" [type: 'Fixed]  ; Fix FIND/LAST refinement when used with BLOCK! 
"22fcd9c" [type: 'Changed]  ; Disallow `<opt>` on refinement arguments 

"40db488" [type: 'Changed trello: https://trello.com/c/uPiz2jLL/]  ; New loop return result policy, eliminate BREAK/RETURN 
; also https://trello.com/c/cOgdiOAD/73-break-return-is-gone-continue-and-continue-with-added-some-new-behavior-for-break-and-break-with
; and https://trello.com/c/Nfwh3Jne/59-break-return-break-with


"9b5e025" [type: 'Changed]  ; Make /LINES and /STRING work on non-FILE! ports (e.g. URL!) 
"d5a216f" [type: 'Changed]  ; Enhance the MATH dialect 
"cc6d287" [type: 'Changed trello: https://trello.com/c/wyXCkx67/]  ; ANY and ALL chain "opt-outs" (return void)...add ANY? and ALL? 
"fe9c43f" no ;[type: 'Changed]  ; Simplify mutable variable access 
"061ca23" [type: 'Changed]  ; Simplify GC disablement, allow GC active during boot 
"42e20da" [type: 'Changed]  ; Mark UNTIL reserved for future use 
"a8c0c19" [type: 'Changed]  ; JOIN => JOIN-OF, reserve JOIN, act like R3-Alpha REPEND 
"c3b442c" [type: 'Fixed]  ; Patch for appending a MAP! to a MAP! (used also by COPY) 
"bb4bc77" [type: 'Fixed]  ; Fix `<in>` feature of FUNCTION and PROCEDURE, use in %sys-start 
"5786074" [type: 'Added trello: https://trello.com/c/Md1pxsvs/]  ; /ASTRAL refinement for TO-STRING 
"0b483bc" [type: 'Changed]  ; Make return value of CLOSE optional 
"fd9e9e6" [type: 'Changed]  ; Make ECHO return void, revert fixture line ending change 

"0970784" [type: 'Added trello: https://trello.com/c/988j1mjS/]  ; LOOP-WHILE, LOOP-UNTIL, and UNTIL-2 

"dbad6c0" [type: 'Changed]  ; Allow FOR-EACH to enumerate all the FUNCTION!s 
"14a5d4d" [type: 'Fixed]  ; Fix CHANGE on struct! 
"dfcd893" [type: 'Removed]  ; Delete unimplemented TASK! stub 
"ab9e27c" [type: 'Fixed]  ; Fix trim object! 
"95841c2" [type: 'Added]  ; Add /deep to mkdir 
"5738a5f" [type: 'Changed]  ; Bulletproof QUOTE in PARSE against END mark on input 
"8c94d2f" [type: 'Added]  ; Add a function to convert windows path to Rebol path 
"f45767d" [type: 'Fixed]  ; Fix sequence point issue in ENBASE 
"587d27a" [type: 'Changed]  ; No literal blocks in condition of IF/UNLESS/EITHER/CASE 

"6caf195" [
    type: 'Added
    trello: https://trello.com/c/1jTJXB0d/
    related: ["61abd10"]
    summary: {ANY-TYPE! synonym for ANY-VALUE}
]  ; ANY-TYPE! synonym for ANY-VALUE! except in r3-legacy FUNC 

"6ee4583" [type: 'Fixed]  ; Fix search for /LAST in FIND assert on short strings 
"5c13b7b" [type: 'Changed]  ; Switch complex module call to APPLY 
"e99e286" [type: 'Changed]  ; Support AS for ANY-WORD! types 
"82c0bf6" [type: 'Fixed]  ; Fix LIST-DIR return result, %mezz-files.r meddling 
"f7d1d6d" [type: 'Changed]  ; Replace HAS-TYPE? specializations with TYPECHECKER 
"4e109a2" [type: 'Added]  ; Add LOGIC! to legal arguments to ASSERT 
"6ea2d48" [type: 'Changed trello: https://trello.com/c/o1ykZaXS/]  ; Prevent failed SET in PARSE from overwriting with BLANK! 
"abd1b89" [type: 'Changed]  ; checked RETURN: types in function specs, notes in HELP 
"e71566f" [type: 'Changed]  ; Hacky hash to allow e.g. unique [[1 2] [1 2]] 
"b7fac96" [type: 'Fixed]  ; Fixes for BLANK! and empty block in PARSE handling, tests 
"49371ea" [type: 'Fixed]  ; Fix DEFAULT of not set bug, add tests 
"dee59da" [type: 'Fixed]  ; Fix PARSE? (was returning void instead of TRUE/FALSE) 

"f0304ed" [
    example: {
>> x: enfix func [a b] [a * b]

>> 2 x 3
== 6
}
    type: 'Added 
    related: ["cfae703"]
    trello: https://trello.com/c/OvNE3GPM/
    summary: {Custom ENFIX operators.  Killed OP!}
] ;  Add "ENFIX", and "enfixed" DEFAULT 

"cfae703" no  ; [type: 'Changed]  ; Custom infix operators via <infix> in spec, kill OP! 

"f09977c" no  ; [type: 'Changed]  ; Undo namings /? => BRANCHED?, MATCHED?, RAN?... 
"24fbc6e" [type: 'Changed]  ; Move `func [[catch] x][...]` support to <r3-legacy> 
"bb49492" [type: 'Fixed]  ; Fix REMOVE copying of implicit terminator, related cleanups 
"d6f2194" [type: 'Changed]  ; Don't evaluate non-blocks passed to PRINT 

"0a28ea5" [
    type: 'Added
    related: ["de3f9a1" "19983a9" "051f2a9" "4e16471" "d5b5b9e" "554dce6"]
    trello: https://trello.com/c/LR9PzUS3/125
    summary: {MAYBE, MAYBE? and ENSURE provide convenient inline type checking}
]  ; MAYBE as native, dialected ASSERT, VERIFY, more 
;"de3f9a1" [type: 'Added]  ; Add MAYBE? variant for MAYBE, improve void handling 
;"19983a9" [type: 'Changed]  ; Update ENSURE with /ONLY, no FALSE by default 
;"051f2a9" [type: 'Added]  ; Add ENSURE mezzanine (with ENSURE/TYPE) 
;"4e16471" [type: 'Changed]  ; Rename ENSURE/ONLY to ENSURE/VALUE 
;"d5b5b9e" [type: 'Changed]  ; Allow ENSURE to return any type 
;"554dce6" [type: 'Changed]  ; Partial ENSURE compatibility routine for R3-Alpha/Rebol2 

"603c1d4" [type: 'Added]  ; Add /WHERE feature to FAIL 
"d549740" no ; [type: 'Changed]  ; REDUCE+COMPOSE use safe enumeration, kill unused refinements 
"2c7d58f" [type: 'Fixed]  ; Fix TUPLE! crash after payload switch, round money 
"08767f2" [type: 'Fixed]  ; Fix PRIN error message 
"31327fd" [type: 'Fixed]  ; Fix two SPEC-OF bugs where param and type not being added to the result correctly. 
"22b7f81" no  ; [type: 'Fixed]  ; Fix infix lookback for SET-WORD! and SET-PATH! 
"c72e8d3" [type: 'Fixed]  ; Fix sequence point problem in `read/lines` 
"87ffb37" [type: 'Fixed]  ; Fix typeset molding 
"29fff21" [type: 'Changed]  ; Unify MAKE and construction syntax, MAKE/TO=>native 
"18b1695" [type: 'Fixed]  ; Patch recursive MAP! molding bug 
"296c3f5" [type: 'Added]  ; Add AS and ALIASES? natives 
"89df6ae" [type: 'Fixed]  ; Fix IS function and add /RELAX refinement 
"fa984cb" [type: 'Added]  ; Add ANY? and ALL? via CHAIN, XXX? descriptions 
"c5ab577" [
    example: {
>> add-one: func [x] [x + 1]

>> mp-ad-ad: chain [:multiply | :add-one | :add-one]

>> mp-ad-ad 10 20
== 202
}
    type: 'Added
    trello: https://trello.com/c/lz9DgFAO/
]  ; First draft of ADAPT and CHAIN, function reorganization 
"45df9cd" [type: 'Changed]  ; REDESCRIBE routine for relabeling functions/args 
"22518a5" [type: 'Changed]  ; Allow GET-PATH! processing to return voids 
"ed8f42c" [type: 'Changed]  ; Support SEMIQUOTED? testing on variadic parameters 
"b8a9939" [type: 'Changed]  ; Allow COPY on BLANK!, FIND and SELECT on FRAME! 
"c1b9127" [type: 'Changed]  ; Allow SPECIALIZE/APPLY of definitional returns 
"2c022f8" [type: 'Added trello: https://trello.com/c/rOvj3fHc/]  ; Add SEMIQUOTE, rename EVALUATED? to SEMIQUOTED? 
"112da01" [type: 'Added]  ; Add || userspace "expression barrier that runs one expr" 
"1daba3f" [
    example: {
>> if? 1 < 2 [print "Branch Taken"]
Branch Taken
== true
}
    type: 'Added 
    trello: https://trello.com/c/yy2ezRXi/
]  ; IF?, UNLESS?, WHILE?, SWITCH?, etc. 
"ccac327" [type: 'Added]  ; Add in a `void` function 
"399ef60" [type: 'Changed]  ; Lock functions, legacy function-bodies-mutable switch 
"37cd465" [type: 'Added]  ; Add PUNCTUATOR? and SET-PUNCTUATOR 
"309b221" [
    example: {
>> 10 + 20 <| print "Hello" 100 + 200
Hello
== 30
}
    type: 'Added 
    trello: https://trello.com/c/fpA2ryJw/
] ;  |> and <| Manipulators 
"4e608f7" [type: 'Changed trello: https://trello.com/c/H9I0261r/]  ; not TRUE/NONE, not WORD/NONE but TRUE/FALSE refinements 
"237b28a" [type: 'Changed]  ; Make FIRST, SECOND, etc. use specialization of PICK 
"4a14b42" [type: 'Changed]  ; Enhance legacy return-none-instead-of-void feature 
"e9bbe4f" [type: 'Changed]  ; Make APPLY and SPECIALIZE accept words/paths 
"3b54770" [type: 'Fixed]  ; fix 2138: parse tag in block 
"ca3e014" no ;[type: 'Changed]  ; Implement INFIX? as property of binding, not function 
"634c463" [type: 'Changed]  ; Variadic Quoting Infix Lambdas (a.k.a. "short function") 
"08fc7e5" [type: 'Changed trello: https://trello.com/c/rmsTJueg/]  ; UNSET => VOID name change 
"bbf615d" [type: 'Removed]  ; Eliminate reified UNSET! and datatype 
"cb9c221" [type: 'Added]  ; Add RUNNING? and PENDING? tests for FRAME! 
"d51d9f6" [type: 'Changed]  ; make path access fail if not set and not GET-PATH! 

"213b804" [type: 'Added trello: https://trello.com/c/vJTaG3w5/]  ; Make lone underscore literal "BLANK!/NONE!" 
; https://trello.com/c/rmsTJueg/

"50fd51d" [type: 'Changed]  ; Re-introduce legality of `x: ()` assignments 
"5ed7c50" [type: 'Fixed]  ; Loose the limit on the number of function arguments 
"133200b" [type: 'Changed]  ; Mold fixes for MAP! 
"1e98be5" [type: 'Removed]  ; Remove use of SPLIT from parser which fails on linux when used with a rule that contains THRU. 
"15ff282" no ; [type: 'Added trello: https://trello.com/c/rKXTXRtA/]  ; Add PRINT/ONLY, only dialect nested PRINTs if literal 
"3aa675b" [type: 'Changed]  ; Move variadic DO to r3-legacy, allow 0-arity DO 
"4797130" [type: 'Removed]  ; Remove Markup Codec 
"c6171b3" [type: 'Changed]  ; Move TITLE-OF to user mode code 
"2a7fcbb" [type: 'Removed]  ; Get rid of RM as unix `remove` 
"08dd85d" [type: 'Changed]  ; Implement SPEC-OF and BODY-OF for specializations 
"c61daa4" [type: 'Changed]  ; OneFunction: Unify functions under FUNCTION! 
"dd18504" [type: 'Changed]  ; CASE special handling of expression barriers 
"a793a6a" [type: 'Changed]  ; Improve error NEAR reporting 
"28ec47b" [type: 'Added]  ; Add VARIADIC?, make EVAL prototype "honest" 
"460ce81" [type: 'Added]  ; Add `ok?` as synonym for `not error?` 
"ff8baf9" [type: 'Changed]  ; Allow BAR! to mean "TAKE to END" 
"a20995a" [type: 'Changed]  ; Move REDUCE and COMPOSE to their own file 
"82ccc96" [type: 'Changed]  ; Legacy switch: `no-reduce-nested-print` 
"bdecddd" no ;[type: 'Changed]  ; PRINT/FAIL new design, recursion, |, BINARY! UTF8 
"240783a" [
    example: {
 >> foo: procedure [] [leave print "This won't print"]
 >> foo
 ; No result

 >> foo: proc [y] [y + 20]
 >> foo 10
 ; No result
}
    type: 'Added
    trello: https://trello.com/c/gJsjAhKQ/
    summary: {New PROC and PROCEDURE function generators}  ; being cheeky here but wanted placeholder!
]  ; <transparent> => <no-return> 
"7643bf4" [type: 'Deprecated trello: https://trello.com/c/HcRtAnE4/]  ; Deprecate CLOSURE => <durable>, PROC, PROCEDURE 
"c7eac45" [type: 'Changed]  ; Get-path for maps distinguishes UNSET from NONE 
"70f0f35" [type: 'Changed]  ; Change PICK action for maps 
"b0fbffa" [type: 'Changed]  ; Make case-insensitive hash value for chars 
"b002615" [
    example: {
>> m: map [a 1 b 2 c 3]
== make map! [
    a 1
    b 2
    c 3
]

>> find m 'b
== true

>> find m 'd 
== false
}
    type: 'Added
    trello: https://trello.com/c/DWm8wJA9/
]  ; Add FIND map key 

"fc6a4ee" [type: 'Added]  ; Add REMOVE/MAP map key 
"dc6cbf4" [type: 'Changed]  ; Recycle 'zombie' keys in maps 
"543faca" [type: 'Changed]  ; Case-sensitive maps 
"7de3ea6" [type: 'Changed]  ; Unicode keys in maps 
"9e8b685" [type: 'Changed]  ; Maps: remove ad hoc code for small maps 
"4294371" [type: 'Changed]  ; Make `GET path` equivalent to `:path` 
"c84a17c" [
    example: {
>> reduce [1 + 1 | 2 + 2 | 4 + 4]
== [2 | 4 | 8]
}
    type: 'Added 
    trello: https://trello.com/c/7RbcHZX3/
]  ; Expression barriers: BAR! and LIT-BAR! 
"354f8fc" [type: 'Added]  ; Add decode-key-value-text function. 
"9e965f5" [type: 'Changed]  ; Upgrade decode-lines. 
"7920880" [type: 'Changed]  ; Improve UTF-8 decoding error messages 
"938491c" [type: 'Changed]  ; Make ASSERT return UNSET! instead of TRUE 
"791c7cd" [type: 'Changed]  ; Distinguish opt-any-value! from any-value! 
"76741d5" [type: 'Fixed]  ; Fix molding of typesets 
"9dbc985" [type: 'Changed]  ; HALT as Ctrl-C instead of BREAKPOINT 
"7d95daa" [type: 'Changed]  ; SET/ANY and GET/ANY => SET/OPT and GET/OPT 
"3c665c3" [type: 'Added]  ; Add CHECK-SET routine per suggestion from @earl 
"b890ae7" [type: 'Changed]  ; Drop SET? as "ANY-VALUE?" operation pending review 
"d216e30" [type: 'Changed]  ; Dividing money! amount again returns money!, preserving precision 
"dcae241" [type: 'Fixed]  ; Fix assert/problem with CLOSURE!+THROW 
"f07ec26" [type: 'Changed]  ; Reword fix, integrate reword tests from @johnk 
"c78d5a2" [type: 'Added trello: https://trello.com/c/ANlT44nH/]  ; GROUP! as new default term for PAREN! 
"41736da" [type: 'Changed trello: https://trello.com/c/3V57JW68/]  ; LOOP accepts logic/none, infinite or no loop 
"93d2677" [type: 'Added trello: https://trello.com/c/FWFFMz68/]  ; Add COLLECT-WITH mezzanine to specify "keeper" 

"fab1090" [
    type: 'Added
    related: ["123be17"]
    trello: https://trello.com/c/QBZGM8GY/
    summary: {Added SOMETHING? & NOTHING?}
]  ; Add SOMETHING? helper (not unset, not none) 

"f7fcfdc" [type: 'Fixed]  ; Fix any-object? alias 

"e8419e6" [
    type: 'Added
    trello: https://trello.com/c/StCADPIB/
    summary: {FOR-SKIP synonym for FORSKIP and FOR-NEXT / FOR-BACK for FORALL}
]  ; FORALL, FORSKIP => FOR-NEXT, FOR-BACK, FOR-SKIP 

"4658148" [type: 'Changed]  ; Adjust DECODE-URL to give back NONE hosts 
"62d2a5a" [type: 'Changed]  ; Convert canon NONE, UNSET, etc. to globals 
"c43dd18" [type: 'Changed]  ; Use NOOP so empty loop looks more intentional 
"6711dc2" [type: 'Added]  ; Breakpoints + Interactive Debugging 
"5e9ec3e" [type: 'Changed trello: https://trello.com/c/noiletwM/]  ; EXIT/FROM any stack level, natives 
"505a7a3" [type: 'Fixed]  ; Fix actor dispatch (read http://, etc.) 
"2294dce" [type: 'Changed]  ; Programmatic GDB breakpoint support 
"9a0bffe" [type: 'Fixed]  ; Fix "make object! none" cases 
"a4fff1d" [type: 'Fixed]  ; Fix offset of default command in dialect parsing 
"19cc56f" [type: 'Fixed]  ; Fix mezz function ARRAY 
"9cad6b8" [type: 'Changed]  ; Tidy new-line implementation, no bitwise XOR 
"f22b554" [type: 'Fixed]  ; Fix precedence bug in file open permissions 
"a48bb4a" [type: 'Changed]  ; Boolean review changes to uni/string flags 
"e833e9e" [type: 'Fixed]  ; Fix WAIT without timeout 
"1c8e2ea" [type: 'Changed trello: https://trello.com/c/RAvLaReG/]  ; POKE on BITSET! only pokes LOGIC! values 
"bdbd45c" [type: 'Changed]  ; Sync LOCK, PROTECT meanings for series/typesets 
"0962677" [type: 'Fixed]  ; Fix suffix? and rename it suffix-of 
"17b2704" [type: 'Changed]  ; make-module* functionality => userspace MODULE 
"7305847" [type: 'Changed]  ; ANY-OBJECT! => ANY-CONTEXT! 
"78173cc" [type: 'Changed]  ; Update DO to experimental PARAM/REFINE model 
"e89502b" [type: 'Changed]  ; Disallow CONTINUE/WITH from an UNTIL 
"ff7199f" [type: 'Changed]  ; Permit URL! to be converted to TUPLE! 
"e8e30e9" [type: 'Added]  ; Add SET? native, remove x: () disablement from legacy 
"8680934" [type: 'Changed]  ; OPTIONAL => OPT, RELAX => TO-VALUE 
"decba66" [type: 'Fixed]  ; Fix apparently longstanding FORM OBJECT! bug 

"c77b6b4" [
    type: 'Added 
    trello: https://trello.com/c/WKGaad6F/
    summary: {<local> tag added to FUNC}
]  ; Add <local> tag to FUNC + CLOS, move <infix> 


"894174b" [type: 'Fixed]  ; Fix case-sensitivity in string sort, support unicode, #2170 
"9888f21" [type: 'Added trello: https://trello.com/c/4Ky7vRCb/]  ; SPELLING-OF as native function 
"227419a" [type: 'Changed]  ; SET-WORD! in func spec are "true locals", permit RETURN: 
"fd5f4d6" [type: 'Changed trello: https://trello.com/c/4Kg3DZ2H/]  ; Definitional Returns. Solved. 
"778da31" [type: 'Changed]  ; Default IF, WHILE, EVERY to unsets, x: () legal 
"6eb91b0" [type: 'Added]  ; Add RELAX and OPTIONAL natives, OPT shorthand 
"bdd51eb" [type: 'Added]  ; Add CONTINUE/WITH, modify BREAK/WITH, cleanup 
"a3f249c" [type: 'Changed]  ; Dividing money amounts returns decimal 
"e9c4fcf" [type: 'Changed trello: https://trello.com/c/I6Y3NWlR/]  ; TO-INTEGER/UNSIGNED and conversion fixes 
"a25c359" [type: 'Removed]  ; Get rid of UTYPE! stub, datatype=>symbol fix 
"bb51e94" [type: 'Changed]  ; Move FUNCT to be in <r3-legacy> mode only 
"46c1d1f" [type: 'Changed]  ; SWITCH uses EQUAL?+STRICT-EQUAL?, more... 
"f1f516d" [type: 'Changed]  ; Move /WORD off of TYPE-OF and onto TYPE? 

"f1e4a20" [
    type: 'Changed 
    cc: [1879]
    trello: https://trello.com/c/bUnWYu4w/
    summary: {AND OR XOR are now conditional operators. New bitwise AND* OR+ XOR+}
]  ; Conditional AND/OR/XOR (#1879) 

"1a4b541" [type: 'Changed]  ; CHANGE-DIR to URL, relative paths for DO of URL 
"eab90ce" [type: 'Changed]  ; Make TAIL? spec support all types EMPTY? does 
"839a3f8" [type: 'Added summary: {New prefix AND? OR? XOR? NOT? functions}]  ; Add AND? OR? XOR? NOT? operations 
"1bd93b2" no  ; [type: 'Changed]  ; RAISE and PANIC psuedo-keywords 
"89e23d3" [
    type: 'Added
    trello: https://trello.com/c/cxvHGNha/
    summary: {Added FOR-EACH / EVERY.  Deprecating FOREACH}
]  ; foreach => for-each/every, foreach as legacy 
"1ffa861" [type: 'Added trello: https://trello.com/c/lCSdxtux/]  ; ANY-LIST! typeset (and ANY-ARRAY! experiment) 

"58007df" [
    type: 'Added
    trello: https://trello.com/c/IbnfBaLI
    related: ["f5c003b"]
    summary: {TRAP, a proposed replacement for TRY. TRY/EXCEPT => TRAP/WITH}
]  ; Replace Pre-Mezzanine TRY => TRAP 

"b85f178" [type: 'Changed]  ; QUIT via THROW, CATCH [] vs CATCH/ANY (CC#2247) 
"afe7762" [type: 'Changed]  ; Implement <transparent> attribute behavior for functions 
"cc08fea" [type: 'Changed]  ; New CASE with switch for legacy behavior (CC#2245) 
"110b9fe" [type: 'Changed]  ; Gabriele Santilli's MATH and FACTORIAL (CC#2120) 
"6b017f8" [type: 'Changed]  ; EXIT acts as QUIT if not in function, LEGACY flags 
"5dc4b48" [type: 'Fixed]  ; Fix out-of-context word in Reword function. 
"242c1e2" [type: 'Changed]  ; [catch] function spec block exemption 
"34bb816" [type: 'Changed]  ; CC#2242 fix of RETURN/THROW in SWITCH 
"85013fe" [type: 'Changed]  ; Enbase fixes for input zero length strings 
"9b21568" [type: 'Changed]  ; Error handling overhaul, includes CC#1743 
"bfc2604" [type: 'Fixed] ;  Fix halt in 'forever []' and similar (CC#2229) 
"49b94e5" [
    type: 'Removed 
    trello: https://trello.com/c/YMAb89dv/  ; not correct trello but does mention it!
]  ; Get rid of RETURN/redo 
"37c723e" [type: 'Changed]  ; Revert "Remove /NOW refinement from QUIT (CC#1743)" 
"5f22bc3" [type: 'Added]  ; Add new pseudo-type "TRASH!" 
"e93a5b4" [type: 'Removed]  ; Remove /NOW refinement from QUIT (CC#1743) 
"9f6e56e" [type: 'Fixed]  ; Fix LAUNCH to properly work with argv-based CALL 
"f027870" [type: 'Removed trello: https://trello.com/c/MVLLDYJW/]  ; Get rid of Lit-Word decay (CC#2101, CC#1434) 
"5267b00" [type: 'Changed]  ; Interim workaround for CC#2221 
"89a75c7" [type: 'Changed]  ; Switch version number to 2.102.0, for the time being 
"5c2c263" [type: 'Fixed]  ; Fix cc-2224 by simply disallowing LENGTH? on ANY-WORD! 
"94b001b" [type: 'Fixed]  ; PowerPC/Big Endian processor unicode fix 
"8e5b9b1" [type: 'Removed]  ; Remove deprecated 'register' keyword 
"d8ae8e8" [type: 'Fixed]  ; Fix break from remove-each 
"3b386a5" [type: 'Fixed]  ; Fix foreach with set-word 
"87cf612" [type: 'Fixed]  ; Fix a divided-by-zero error 
"d417aa2" [type: 'Added]  ; Add UTF-16LE/BE codec for strings 
"bc6c9b6" [type: 'Changed]  ; Accept file! to CALL 
"e750611" [type: 'Changed]  ; Take binary! for I/O redirection in CALL 
"cd435b5" [type: 'Changed]  ; Make /output in CALL take string! instead of word! 
"b489d85" [type: 'Changed]  ; Implement a R2-like non-blocking CALL 
"1dd682e" [type: 'Changed]  ; CALL with IO redirection 
"02e7cea" [type: 'Changed]  ; Do not use the system qsort 
"1286ac5" [type: 'Added]  ; Add an encapper for Windows and Linux 
"3a04abf" [type: 'Added related: ["adeaa40"]]  ; FEAT: Windows Serial Implementation and extension of serial functionality 
"7985d07" [type: 'Added]  ; Add submodule libffi and link to static libffi 
"dcf71dc" [type: 'Changed]  ; make library! works 
"5e5e1a5" [type: 'Changed]  ; TO LOGIC! treats all non-none non-false values as true (CC #2055) 
"23750aa" [type: 'Fixed]  ; Fix PARSE position capture combined with SET or COPY 
"89c9af5" [type: 'Changed]  ; IF, EITHER, and UNLESS accept all values in then/else slots (CC #2063) 

"66b87c9" [
    example: {
foo: function [s] [
    parse s [
        {My name is } copy name: to end
    ]
    name
]
}
    type: 'Added
]  ; Permit SET-WORD! as the argument to COPY and SET in PARSE (CC #2023) 

"8eed3cd" [type: 'Changed]  ; Use R3's rounding implementation instead of round(3). 
"2fd94f2" [type: 'Changed]  ; Round decimal division of tuple elements. 
"e0296bc" [type: 'Fixed]  ; Fix vector! for 64-bit systems 
"689c95f" [type: 'Fixed]  ; Fix MD5 on 64-bit Linux 
"65d0b18" [type: 'Fixed]  ; Fix 32-bit type used in MD5 
"b339b0f" [type: 'Changed]  ; Make CLOSURE a FUNCT for closures (#2002) 
"4c6de3b" [type: 'Changed]  ; Replace FUNCT with FUNCTION 
"3e6ecf7" [type: 'Changed]  ; Make FUNCTION a FUNCT alias (#1773) 
"21fb3c2" [type: 'Changed]  ; cc-1748: Guard protected blocks from /INTO target of REDUCE, COMPOSE 
"e1d8d05" [type: 'Changed]  ; Free intermediate buffers used by do-codec (CC #2068) 
"75f7320" [type: 'Added]  ; Added support for ESC-O escape sequences in addition to ESC-[ 
"8da64f8" [type: 'Removed]  ; Get rid of LOAD /next 
"bdd7523" [type: 'Changed]  ; Copied DELETE-DIR from Rebol 2 (cc#1545) 
"23673df" [type: 'Removed]  ; Get rid of if/else 
"ecfcde7" [type: 'Fixed]  ; fix ticket1457 parse to bitset 
"a5fea46" [type: 'Fixed]  ; -fixed zlib bug caused PNG encode operation to crash 
"f929c97" [type: 'Changed]  ; Implement UDP protocol 
"895c893" [type: 'Changed]  ; Incorporate /ONLY option for suppressing conditional block evaluation. 
"038555b" [type: 'Changed]  ; Support async read from clipboard 
"4f17ba6" [type: 'Changed]  ; String & binary targets for /INTO in REDUCE+COMPOSE (CC #2081) 
"dd11362" [type: 'Changed]  ; Use Bentley & McIlroy's qsort for sorting strings and blocks Currently, R3 uses platform-specific code for sorting. This may increase the effort necessary to port the interpreter to new platforms. 
"16c6867" [type: 'Fixed]  ; Update fix of CC#851 and CC#1896 
"ee69898" [type: 'Fixed]  ; Fix CATCH/quit interaction with TRY (cc#851) 
"7ef62e8" [type: 'Fixed]  ; Fix CATCH/QUIT, CC#851, CC#1896. , the bug causing 3 test-framework crashes. 
"7372289" [type: 'Changed]  ; Prevent word duplication when appending to an object Fix CC#1979 Optimize the code to not use search 
"bd83d2b" [type: 'Fixed]  ; Fix circular block compare crash, CC#1049 
"b68ddee" [type: 'Changed]  ; HTTP: remove auto-decoding of UTF-8 content 
"264bb4e" [type: 'Changed]  ; Simplify value? change (pull 121, cc 1914) by using existing function. 
"65a8017" [type: 'Fixed]  ; Correct problem with HTTP READ after correcting bug#2025 
"66876d4" [type: 'Fixed]  ; Let BIND bind out-of scope function words. Corrects bug#1983. 
"1b259d0" [type: 'Changed]  ; Let the VAUE? function yield #[false] for out-of-scope function variables, which is what the documentation describes. Corrects bug#1914 
"93b05f7" [type: 'Removed]  ; Remove message print when LOAD/NEXT is used. bug#2041 
"2b4c5f7" [type: 'Added]  ; -added ADLER32 checksum -enhanced RSA for /padding refinement 
"dac7455" [type: 'Fixed]  ; Fix SET object/block block assuming /any (#1763) 
"eaf0d94" [type: 'Fixed]  ; Fix PARSE regression: THRU not matching at the end 
"824aab6" [type: 'Fixed]  ; bug#1957 corr, FIRST+ help strings updated 
"4ac3424" [type: 'Fixed]  ; bug#1958, BIND and UNBIND can work on blocks not containing (just) words 
"5ff6627" [type: 'Fixed]  ; bug#1844, modification properties 
"276eed3" [type: 'Fixed]  ; bug#1955, updates of malconstruct error and mold help string 
;"0d68c29" [type: 'Fixed]  ; bug#1956 
"f4ce48e" [type: 'Changed]  ; Use David M. Gay's dtoa for molding decimals 
"631e698" [type: 'Changed]  ; Allow NEW-LINE and NEW-LINE? to accept PAREN! series 
"b1e845b" [type: 'Fixed]  ; bug#1939 
"370d942" [type: 'Changed]  ; Allow INDEX? of NONE, returning NONE 
"d2dce76" [type: 'Fixed]  ; Fix crash when reading dns:// without a hostname 
"b9426a5" [type: 'Added]  ; add SET-ENV, tweaks to host api for environment string handling 
"b13af70" [type: 'Changed]  ; Support common Ctrl-D usage as Delete. 
"0eec0cd" [type: 'Changed]  ; Support Home and End keys on linux. 
"af37b35" [type: 'Fixed]  ; Fixed #1875 - random/only bug 
"00bee60" [type: 'Fixed]  ; Fix CC#1865: resolve/extend/only crash 
"71e80bd" [type: 'Changed]  ; REQUEST-FILE/multi returning multiple files is missing the last / in the filenames 
"08eb7e8" [type: 'Added version: "R3-Alpha"]  ; Initial source release 
"8b682e5" [type: 'Added]  ; R3 license 
"19d4f96" [type: 'Added]  ; Initial commit 

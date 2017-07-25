;
; These are tests which were extracted from core-tests that were flagged
; #r2only.  That indicated that they should not be run under Rebol3.
; (Tests which were exclusive to Rebol3 were tagged #r3only.)
;
; For the Ren/C testing effort, Rebol2 testing is no longer being run,
; and a maintenance of a distinction of #r3only or #ren-c-only would
; burden the addition of further tests.
;
; BUT rather than delete the #r2only test entries entirely just yet, they
; are archived here *in case they represent an unimplemented feature*.
; (That is, as opposed to a purposefully deprecated behavior.)
;
; It is no longer expected that Ren/C's copy of the core-tests file be
; able to be used with Rebol2.  For a version that can do that, see:
;
;     https://github.com/rebolsource/rebol-test
;
; -HF
;

[datatype? hash!]
[datatype? list!]
[datatype? routine!]
[datatype? symbol!]
; error types
[error? make error! [throw no-loop]]
[error? make error! [throw no-function]]
[error? make error! [throw no-catch]]
[error? make error! [note no-load]]
[error? make error! [note exited]]
[error? make error! [syntax invalid]]
[error? make error! [syntax missing]]
[error? make error! [syntax header]]
[error? make error! [script no-value]]
[error? make error! [script need-value]]
[error? make error! [script no-arg]]
[error? make error! [script expect-arg]]
[error? make error! [script expect-set]]
[error? make error! [script invalid-arg]]
[error? make error! [script invalid-op]]
[error? make error! [script no-op-arg]]
[error? make error! [script no-return]]
[error? make error! [script not-defined]]
[error? make error! [script no-refine]]
[error? make error! [script invalid-path]]
[error? make error! [script cannot-use]]
[error? make error! [script already-used]]
[error? make error! [script out-of-range]]
[error? make error! [script past-end]]
[error? make error! [script no-memory]]
[error? make error! [script block-lines]]
[error? make error! [script invalid-part]]
[error? make error! [script wrong-denom]]
[error? make error! [script else-gone]]
[error? make error! [script bad-compression]]
[error? make error! [script bad-prompt]]
[error? make error! [script bad-port-action]]
[error? make error! [script needs]]
[error? make error! [script locked-word]]
[error? make error! [script too-many-refines]]
[error? make error! [script dup-vars]]
[error? make error! [script feature-na]]
[error? make error! [script bad-bad]]
[error? make error! [script limit-hit]]
[error? make error! [script call-fail]]
[error? make error! [script face-error]]
[error? make error! [script face-reused]]
[error? make error! [script bad-refine]]
[error? make error! [math zero-divide]]
[error? make error! [math overflow]]
[error? make error! [math positive]]
[error? make error! [access cannot-open]]
[error? make error! [access not-open]]
[error? make error! [access already-open]]
[error? make error! [access already-closed]]
[error? make error! [access read-error]]
[error? make error! [access invalid-spec]]
[error? make error! [access socket-open]]
[error? make error! [access no-connect]]
[error? make error! [access no-delete]]
[error? make error! [access no-rename]]
[error? make error! [access no-make-dir]]
[error? make error! [access protocol]]
[error? make error! [access timeout]]
[error? make error! [access new-level]]
[error? make error! [access security]]
[error? make error! [access invalid-path]]
[error? make error! [access bad-image]]
[error? make error! [access would-block]]
[error? make error! [access serial-timeout]]
[error? make error! [access write-error]]
[error? make error! [command fmt-too-short]]
[error? make error! [command fmt-no-struct-size]]
[error? make error! [command fmt-no-struct-align]]
[error? make error! [command fmt-bad-word]]
[error? make error! [command fmt-type-mismatch]]
[error? make error! [command fmt-size-mismatch]]
[error? make error! [command dll-arg-count]]
[error? make error! [command empty-command]]
[error? make error! [command db-not-open]]
[error? make error! [command db-too-many]]
[error? make error! [command cant-free]]
[error? make error! [command nothing-to-free]]
[error? make error! [command ssl-error]]
[error? make error! [user message]]
[error? make error! [internal bad-path]]
[error? make error! [internal not-here]]
[error? make error! [internal stack-overflow]]
[error? make error! [internal globals-full]]
[error? make error! [internal bad-internal]]
[function? first [#[function! [] []]]]
[gf: func [:x] [:x] a: 10 10 == gf a]
; Argument passing of "literal arguments" ("lit-args")
[lf: func ['x] [:x] (quote (10 + 20)) == lf (10 + 20)]
[lf: func ['x] [:x] (quote :o/f) == lf :o/f]
; basic test for recursive function! invocation
[
    ; context-less get-word
    e: disarm try [do to block! ":a"]
    e/id = 'not-bound
]
; behaviour for REBOL datatypes; unset
[
    unset 'a
    e: disarm try [:a]
    e/id = 'no-value
]
; minimum
[hash? make hash! []]
[not hash? 1]
[hash! = type? make hash! []]
; datatypes/image.r
[
    a-value: #[image! 1x1 #{}]
    equal? pick a-value 0x0 0.0.0.0
]
[issue? #]
[# == #[issue! ""]]
[# == make issue! 0]
[# == to issue! ""]
[list? make list! []]
[not list? 1]
[list! = type? make list! []]
; datatypes/lit-path.r
[3 == do reduce [get '+ 1 2]]
[
    a-value: make image! 1x1
    0.0.0.0 == a-value/1
]
[
    a-value: #2
    #"2" == a-value/1
]
[
    a-value: make port! http://
    none? a-value/user-data
]
[symbol! = type? make symbol! "xx"]
; datatypes/tag.r
[datatype? any-block!]
[datatype? any-function!]
[datatype? any-string!]
[datatype? any-word!]
[datatype? any-number!]
[datatype? any-series!]
[
    error? a-value: try [1 / 0]
    same? disarm :a-value disarm a-value
]
; lit-paths are word-active
[
    a-value: first ['a/b]
    a-value == to path! :a-value
]
; ops are word-active
[
    a-value: get '+
    3 == a-value 1 2
]
[
    a-value: make struct! [] none
    same? third :a-value third a-value
]
[
    unset 'a-value
    e: disarm try [a-value]
    e/id = 'no-value
]
; image! alpha not specified = 0
[equal? #[image! 1x1 #{000000} #{00}] #[image! 1x1 #{000000}]]
; date! ignores time portion
[equal? 2-Jul-2009 2-Jul-2009/22:20]
[error? try [equal? () ()]]
[error? try [equal? () none]]
[error? try [equal? none ()]]
[not equal? disarm try [equal? none ()] disarm try [equal? () none]]
[error? try [none = ()]]
[error? try [none != ()]]
[error? try [() = ()]]
[error? try [() != ()]]
[
    a-value: first ['a/b]
    parse :a-value [b-value:]
    not same? :a-value :b-value
]
[
    a-value: first [()]
    parse a-value [b-value:]
    not same? a-value b-value
]
[
    a-value: 'a/b
    parse a-value [b-value:]
    not same? :a-value :b-value
]
[
    a-value: first [a/b:]
    parse :a-value [b-value:]
    not same? :a-value :b-value
]
[same? 'a first [:a]]
[same? 'a first ['a]]
[same? 'a first [a:]]
[same? first [:a] first ['a]]
[same? first [:a] first [a:]]
[same? first ['a] first [a:]]
[
    a-value: first ['a/b]
    parse :a-value [b-value:]
    not strict-equal? :a-value :b-value
]
[
    a-value: first [()]
    parse a-value [b-value:]
    not strict-equal? a-value b-value
]
[
    a-value: 'a/b
    parse a-value [b-value:]
    not strict-equal? :a-value :b-value
]
[
    a-value: first [a/b:]
    parse :a-value [b-value:]
    not strict-equal? :a-value :b-value
]
[strict-equal? 2-Jul-2009 2-Jul-2009/22:20]
[strict-equal? 2-Jul-2009 2-Jul-2009/00:00:00+00:00]
[use [a] [unset? get/only 'a]]
[unset? any [()]]
[unset? any [false ()]]
[unset? any [() false]]
[-2 == apply :- [2]]
[logic! = case type? [true []]]
[object? disarm try [1 / 0]]
; functions/control/do.r
[
    a-value: #{}
    same? a-value do a-value
]
[
    a: 12
    a-value: first [:a]
    :a-value == do :a-value
]
[
    a-value: first ['a]
    :a-value == do :a-value
]
[
    a-value: first [a:]
    :a-value == do :a-value
]
[
    success: false
    do/next [success: true success: false]
    success
]
[[1 [2]] = do/next [1 2]]
[unset? first do/next []]
; RETURN stops the evaluation
[
    f1: does [do/next [return 1 2] 2]
    1 = f1
]
[
    blk: [do/next blk]
    error? try blk
]
; are error reports for do and do/next consistent?
[
    val1: disarm try [do [1 / 0]]
    val2: disarm try [do/next [1 / 0]]
    val1/near = val2/near
]
[error? err: try [else] c: disarm err c/id = 'else-gone]
; char tests
[
    num: 0
    char: #"^(ff)"
    not for i char char 1 [
        num: num + 1
        if num > 1 [break]
    ]
]
[
    num: 0
    char: #"^(0)"
    not for i char char -1 [
        num: num + 1
        if num > 1 [break]
    ]
]
[
    b: head insert copy [] try [1 / 0]
    pokus1: func [[catch] block [block!] /local elem] [
        for i 1 length? block 1 [
            if error? set/only 'elem first block [
                throw make error! {Dangerous element}
            ]
            block: next block
        ]
    ]
    b: disarm try [pokus1 b]
    b/near = [pokus1 b]
]
; in Rebol2 the FORALL function is unable to pass a THROW error test
[
    f: func [[catch] /local x] [
        x: [1]
        forall x [throw make error! ""]
    ]
    e: disarm try [f]
    e/near = [f]
]
[
    blk: copy out: copy []
    for i #"A" #"Z" 1 [append blk i]
    forskip blk 2 [append out blk/1]
    out = [#"A" #"C" #"E" #"G" #"I" #"K" #"M" #"O" #"Q" #"S" #"U" #"W" #"Y"]
]
; in Rebol2 the FORSKIP function is unable to pass a THROW error test
[
    f: func [[catch] /local x] [
        x: [1]
        forskip x 1 [throw make error! ""]
    ]
    e: disarm try [f]
    e/near = [f]
]
; string! test
[
    out: copy ""
    repeat i "abc" [append out i]
    out = "abc"
]
; block! test
[
    out: copy []
    repeat i [1 2 3] [append out i]
    out = [1 2 3]
]
; local variable type safety
[
    test: false
    repeat i 2 [
        either test [i == 2] [
            test: true
            i: false
            true
        ]
    ]
]
[
    e: disarm try [1 / 0]
    e/id = 'zero-divide
]
[
    a: "a"
    b: as-binary a
    b == to binary! a
    change a "b"
    b == to binary! a
]
[
    a: #{00}
    b: as-string a
    b == to string! a
    change a #{01}
    b == to string! a
]
[block? load/next "1"]
; bug#1703  bug#1711
[
    any [
        not error? e: try [make-dir %/folder-to-save-test-files]
        (e: disarm e e/type = 'access)
    ]
]
[0x0 = add -2147483648x-2147483648 -2147483648x-2147483648]
[2147483647x2147483647 = add -2147483648x-2147483648 -1x-1]
[-2147483647x-2147483647 = add -2147483648x-2147483648 1x1]
[-1x-1 = add -2147483648x-2147483648 2147483647x2147483647]
[2147483647x2147483647 = add -1x-1 -2147483648x-2147483648]
[2147483646x2147483646 = add -1x-1 2147483647x2147483647]
[-2147483647x-2147483647 = add 1x1 -2147483648x-2147483648]
[-2147483648x-2147483648 = add 1x1 2147483647x2147483647]
[-1x-1 = add 2147483647x2147483647 -2147483648x-2147483648]
[2147483646x2147483646 = add 2147483647x2147483647 -1x-1]
[-2147483648x-2147483648 = add 2147483647x2147483647 1x1]
[-2x-2 = add 2147483647x2147483647 2147483647x2147483647]
; pair + ...
[#"^(00)" = add #"^(01)" #"^(ff)"]
[#"^(00)" = add #"^(ff)" #"^(01)"]
[#"^(fe)" = add #"^(ff)" #"^(ff)"]
; tuple
; string
["^(03)^(00)" and* "^(02)^(00)" = "^(02)^(00)"]
; functions/math/arccosine.r
; char
[#"^(ff)" = complement #"^@"]
[#"^@" = complement #"^(ff)"]
[#"^(fe)" = complement #"^(01)"]
[#"^(01)" = complement #"^(fe)"]
; tuple
; string
["^(ff)" = complement "^@"]
["^@" = complement "^(ff)"]
["^(fe)" = complement "^(01)"]
["^(01)" = complement "^(fe)"]
; bitset
[
    (make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF})
        = complement make bitset! #{0000000000000000000000000000000000000000000000000000000000000000}
]
[
    (make bitset! #{0000000000000000000000000000000000000000000000000000000000000000})
        = complement make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF}
]
[-2147483648x-2147483648 = negate -2147483648x-2147483648]
; money
[
    (make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF})
        = negate make bitset! #{0000000000000000000000000000000000000000000000000000000000000000}
]
[
    (make bitset! #{0000000000000000000000000000000000000000000000000000000000000000})
        = negate make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF}
]
; char
[not negative? #"^@"]
[not negative? #"^a"]
[not negative? #"^(ff)"]
; money
[false = not make hash! []]
[false = not make list! []]
; char
[not positive? #"^@"]
[positive? #"^a"]
[positive? #"^(ff)"]
; money
[error? try [round/even 2147483648.0]]
[error? try [round/even 9.2233720368547799e18]]
[$0.0 == ($0.000'000'000'000'001 - round/even/to $0.000'000'000'000'001'1 1e-15)]
[not negative? 1e-31 - abs (to money! 26e-17) - round/even/to $0.000'000'000'000'000'255 to money! 1e-17]
[$2.6 == round/even/to $2.55 1E-1]
[not negative? (to money! 1e-31) - abs (to money! -26e-17) - round/even/to -$0.000'000'000'000'000'255 to money! 1e-17]
[2147483647x2147483647 = subtract -2147483648x-2147483648 1x1]
[1x1 = subtract -2147483648x-2147483648 2147483647x2147483647]
[2147483647x2147483647 = subtract -1x-1 -2147483648x-2147483648]
[-2147483648x-2147483648 = subtract -1x-1 2147483647x2147483647]
[-2147483648x-2147483648 = subtract 0x0 -2147483648x-2147483648]
[-2147483647x-2147483647 = subtract 1x1 -2147483648x-2147483648]
[-2147483646x-2147483646 = subtract 1x1 2147483647x2147483647]
[-1x-1 = subtract 2147483647x2147483647 -2147483648x-2147483648]
[-2147483648x-2147483648 = subtract 2147483647x2147483647 -1x-1]
[2147483646x2147483646 = subtract 2147483647x2147483647 1x1]
[#"^(00)" = subtract #"^(00)" #"^(00)"]
[#"^(ff)" = subtract #"^(00)" #"^(01)"]
[#"^(01)" = subtract #"^(00)" #"^(ff)"]
[#"^(01)" = subtract #"^(01)" #"^(00)"]
[#"^(00)" = subtract #"^(01)" #"^(01)"]
[#"^(02)" = subtract #"^(01)" #"^(ff)"]
[#"^(ff)" = subtract #"^(ff)" #"^(00)"]
[#"^(fe)" = subtract #"^(ff)" #"^(01)"]
[#"^(00)" = subtract #"^(ff)" #"^(ff)"]
; tuple
[error? try [find none 1]]
[
    a: make issue! 0
    insert a #"0"
    a == #0
]
[
    a: #0
    b: make issue! 0
    insert b first a
    a == b
]
[
    a: #0
    b: make issue! 0
    insert b a
    a == b
]
[
    a: make binary! 0
    insert a #"^(00)"
    a == #{00}
]
[error? try [first []]]
[error? try [second []]]
[error? try [third []]]
[error? try [fourth []]]
[error? try [fifth []]]
[error? try [sixth []]]
[error? try [seventh []]]
[error? try [eighth []]]
[error? try [ninth []]]
[error? try [tenth []]]
[
    i: 0
    parse "a" [any [(i: i + 1 j: if i = 2 [[end skip]]) j]]
    i == 2
]
[1 = pick at [1 2 3 4 5] 3 -2]
[2 = pick at [1 2 3 4 5] 3 -1]
[none? pick at [1 2 3 4 5] 3 0]
[#"1" = pick at "12345" 3 -2]
[#"2" = pick at "12345" 3 -1]
[none? pick at "12345" 3 0]

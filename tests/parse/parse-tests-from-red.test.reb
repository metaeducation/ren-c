; These tests were translated from %parse-test.red
;
; https://github.com/red/red/blob/master/tests/source/units/parse-test.red
;
; Red [
;   Title: "Red PARSE test script"
;   Author: "Nenad Rakocevic"
;   File: %parse-test.reds
;   Tabs: 4
;   Rights: "Copyright (C) 2011-2015 Red Foundation. All rights reserved."
;   License: "BSD-3 - https://github.com/red/red/blob/origin/BSD-3-License.txt"
; ]
;
; BSD-3 Code May Be Included or extended in an Apache 2.0 Licensed Project:
;
; http://www.apache.org/legal/resolved.html#category-a
;
; (Though Apache 2.0 licenses do not permit taking code to BSD-3, permission is
; granted for any of the Ren-C PARSE tests to be taken back as BSD-3 by the
; Red project if they wish.)
;
; Adjustments were made to Ren-C's UPARSE where possible.  These tests will
; eventually be reorganized into appropriate files for the combinators used.

[
    "block"

    (uparse? [] [])
    (uparse? [a] ['a])
    (not uparse? [a] ['b])
    (uparse? [a b] ['a 'b])
    (uparse? [a #b] ['a #b])
    (uparse? [a] [['a]])
    (uparse? [a b] [['a] 'b])
    (uparse? [a b] ['a ['b]])
    (uparse? [a b] [['a] ['b]])
    (uparse? ["hello"] ["hello"])
    (uparse? [#a] [#b | #a])
    (not uparse? [a b] ['b | 'a])
    (uparse? [#a] [[#b | #a]])
    (not uparse? [a b] [['b | 'a]])
    (uparse? [a b] [['a | 'b] ['b | 'a]])
    (uparse? [a 123] ['a integer!])
    (not uparse? [a 123] ['a char!])
    (uparse? [a 123] [['a] [integer!]])
    (not uparse? [a 123] ['a [char!]])
    (uparse? [123] [any-number!])
    (not uparse? [123] [any-string!])
    (uparse? [123] [[any-number!]])
    (not uparse? [123] [[any-string!]])
    (
        res: 0
        did all [
            uparse? [] [(res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] ['a (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a] ['b (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? [] [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [['a (res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a] [['b (res: 1)]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? [a 123] ['a (res: 1) [char! (res: 2) | integer! (res: 3)]]
            res = 3
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a 123] ['a (res: 1) [char! (res: 2) | text! (res: 3)]]
            res = 1
        ]
    )
    (not uparse? [a a] [1 ['a]])
    (uparse? [a a] [2 ['a]])
    (not uparse? [a a] [3 ['a]])
    (not uparse? [a a] [1 1 ['a]])
    (uparse? [a a] [1 2 ['a]])
    (uparse? [a a] [2 2 ['a]])
    (uparse? [a a] [2 3 ['a]])
    (not uparse? [a a] [3 4 ['a]])
    (not uparse? [a a] [1 'a])
    (uparse? [a a] [2 'a])
    (not uparse? [a a] [3 'a])
    (not uparse? [a a] [1 1 'a])
    (uparse? [a a] [1 2 'a])
    (uparse? [a a] [2 2 'a])
    (uparse? [a a] [2 3 'a])
    (not uparse? [a a] [3 4 'a])
    (not uparse? [a a] [1 <any>])
    (uparse? [a a] [2 <any>])
    (not uparse? [a a] [3 <any>])
    (not uparse? [a a] [1 1 <any>])
    (uparse? [a a] [1 2 <any>])
    (uparse? [a a] [2 2 <any>])
    (uparse? [a a] [2 3 <any>])
    (not uparse? [a a] [3 4 <any>])
    (uparse? [a] [<any>])
    (uparse? [a b] [<any> <any>])
    (uparse? [a b] [<any> [<any>]])
    (uparse? [a b] [[<any>] [<any>]])
    (uparse? [a a] [some ['a]])
    (not uparse? [a a] [some ['a] 'b])
    (uparse? [a a b a b b b a] [some [<any>]])
    (uparse? [a a b a b b b a] [some ['a | 'b]])
    (not uparse? [a a b a b b b a] [some ['a | 'c]])
    (uparse? [a a] [while ['a]])
    (uparse? [a a] [some ['a] while ['b]])
    (uparse? [a a b b] [2 'a 2 'b])
    (not uparse? [a a b b] [2 'a 3 'b])
    (uparse? [a a b b] [some 'a some 'b])
    (not uparse? [a a b b] [some 'a some 'c])
    (
        p: blank
        did all [
            uparse? [] [p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? [] [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? [a] [p: <here> 'a]
            p = [a]
        ]
    )
    (
        p: blank
        did all [
            uparse? [a] ['a p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? [a] ['a [p: <here>]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            not uparse? [a b] ['a p: <here>]
            p = [b]
        ]
    )
    (
        p: blank
        did all [
            uparse? [a b] ['a [p: <here>] ['b | 'c]]
            p = [b]
        ]
    )
    (
        p: blank
        did all [
            uparse? [a a a b b] [3 'a p: <here> 2 'b seek (p) [2 'b]]
            p = [b b]
        ]
    )
    (uparse? [b a a a c] [<any> some ['a] 'c])
]

[
    "block-end"

    (uparse? [a] ['a <end>])
    (not uparse? [a b] ['a <end>])
    (uparse? [a] [<any> <end>])
    (not uparse? [a b] [<any> <end>])
    (uparse? [] [<end>])
    (
        be6: 0
        did all [
            uparse? [] [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

[
    "block-words"

    (
        wa: ['a]
        wb: ['b]
        wca: #a
        wcb: #b
        wra: [wa]
        wrb: [wb]
        wh: "hello"
        wrab: ['a | 'b]
        wrba: ['b | 'a]
        true
    )
    (uparse? [a] [wa])
    (not uparse? [a] [wb])
    (uparse? [a b] [wa wb])
    (uparse? [a #b] [wa wcb])
    (uparse? [a] [wra])
    (uparse? [a b] [wra 'b])
    (uparse? [a b] ['a wrb])
    (uparse? [a b] [wra wrb])
    (uparse? ["hello"] [wh])
    (uparse? [#a] [wcb | wca])
    (not uparse? [a b] [wb | wa])
    (uparse? [#a] [[wcb | wca]])
    (not uparse? [a b] [wrba])
    (uparse? [a b] [wrab wrba])
    (uparse? [a 123] [wa integer!])
    (not uparse? [a 123] [wa char!])
    (uparse? [a 123] [wra [integer!]])
    (not uparse? [a 123] [wa [char!]])
    (
        res: 0
        did all [
            uparse? [a] [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a] [wb (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        wres: [(res: 1)]
        did all [
            uparse? [] [wres]
            res = 1
        ]
    )
    (
        res: 0
        wres: ['a (res: 1)]
        did all [
            uparse? [a] [wres]
            res = 1
        ]
    )
    (
        res: 0
        wres: ['b (res: 1)]
        did all [
            not uparse? [a] [wres]
            res = 0
        ]
    )
    (
        res: 0
        wres: [char! (res: 2) | integer! (res: 3)]
        did all [
            uparse? [a 123] [wa (res: 1) wres]
            res = 3
        ]
    )
    (
        res: 0
        wres: [char! (res: 2) | text! (res: 3)]
        did all [
            not uparse? [a 123] [wa (res: 1) wres]
            res = 1
        ]
    )
]

[
    "block-extraction"

    (
        wa: ['a]
        true
    )
    (
        res: 0
        did all [
            uparse? [a] [res: across <any>]
            res = [a]
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: across 'a]
            res = [a]
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: across word!]
            res = [a]
        ]
    )
    (
        res: 0
        res2: 0
        did all [
            uparse? [a] [res: across res2: across 'a]
            res = [a]
            res2 = [a]
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a] [res: across 2 'a]
            res = [a a]
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a a] [res: across 3 'a]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: across ['a]]
            res = [a]
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: across wa]
            res = [a]
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a] [res: across 2 wa]
            res = [a a]
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a b] [<any> res: across 'a <any>]
            res = [a]
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a b] [<any> res: across ['a | 'b] <any>]
            res = [a]
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a] [res: across ['c | 'b]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: <any>]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: 'a]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: word!]
            res = 'a
        ]
    )
    (
        res: 0
        res2: 0
        did all [
            uparse? [a] [res: res2: 'a]
            res = 'a
            res2 = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a] [res: 2 'a]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a a] [res: 3 'a]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: ['a]]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [a] [res: wa]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a] [res: 2 wa]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a b] [<any> res: 'a <any>]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [a a b] [<any> res: ['a | 'b] <any>]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            not uparse? [a] [res: ['c | 'b]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? [b a a a c] [<any> res: some 'a 'c]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            uparse? [b a a a c] [<any> res: some wa 'c]
            res = 'a
        ]
    )
    (
        res: uparse? [] [collect []]
        res = []
    )
    (
        res: uparse? [1] [collect []]
        res = []
    )
    (
        res: uparse? [1] [collect [keep <any>]]
        res = [1]
    )
    (
        res: uparse? [1 2 3] [collect [some [keep integer!]]]
        res = [1 2 3]
    )
    (
        res: uparse? [1 2 3] [collect [some [keep [v: integer! :(even? v)] | <any>]]]
        res = [2]
    )
    (
        res: uparse? [a 3 4 t "test" 8] [collect [while [keep integer! | <any>]]]
        res = [3 4 8]
    )
    (
        a: blank
        did all [
            uparse? [] [a: collect []]
            a = []
        ]
    )
    (
        a: blank
        did all [
            uparse? [1] [a: collect [keep <any>]]
            a = [1]
        ]
    )
    (
        list: blank
        did all [
            uparse? [a 3 4 t "test" 8] [
                list: collect [while [keep integer! | <any>]]
            ]
            list = [3 4 8]
        ]
    )
    (
        res: uparse [a b b b] [collect [<any>, keep ^ across some 'b]]
        res = [[b b b]]
    )
    (
        res: uparse [a b b b] [collect [<any>, keep across some 'b]]
        res = [b b b]
    )
    (
        res: uparse [a b b b] [collect [<any>, keep ^ some 'b]]
        res = [b]
    )
]

[
    "block-skipping"

    (
        wa: ['a]
        true
    )
    (uparse? [] [to <end>])
    (uparse? [] [thru <end>])
    (uparse? [a] [to <end>])
    (not uparse? [a] [to 'a])
    (not uparse? [a] [to 'a <end>])
    (uparse? [a] [to 'a <any>])
    (uparse? [a] [thru 'a])
    (uparse? [a] [thru 'a <end>])
    (not uparse? [a] [thru 'a <any>])
    (uparse? [a b] [to 'b <any>])
    (uparse? [a b] [thru 'b])
    (uparse? [a a a b] [to 'b <any>])
    (uparse? [a a b a] [<any> to 'b 2 <any>])
    (not uparse? [a] [to ['a]])
    (not uparse? [a] [to ['a] <end>])
    (uparse? [a] [to ['a] <any>])
    (uparse? [a] [thru ['a]])
    (uparse? [a] [thru ['a] <end>])
    (not uparse? [a] [thru ['a] <any>])
    (uparse? [a b] [to ['b] <any>])
    (uparse? [a b] [thru ['b]])
    (uparse? [a a a b] [to ['b] <any>])
    (uparse? [a a b a] [<any> to ['b] 2 <any>])
    (uparse? [z z a b c] [to ['c | 'b | 'a] 3 <any>])
    (uparse? [z z a b c] [to ['a | 'b | 'c] 3 <any>])
    (uparse? [z z a b c] [thru ['c | 'b | 'a] 2 <any>])
    (uparse? [z z a b c] [thru ['a | 'b | 'c] 2 <any>])
    (uparse? [b b a a c] [thru 2 'a 'c])
    (uparse? [b b a a c] [thru 2 'a 'c])
    (uparse? [b b a a c] [thru [2 'a] 'c])
    (uparse? [b b a a c] [thru some 'a 'c])
    (uparse? [b b a a c] [thru [some 'a] 'c])
    (uparse? [b b a a c] [thru [some 'x | 2 'a] 'c])
    (uparse? [b b a a c] [thru 2 wa 'c])
    (uparse? [b b a a c] [thru some wa 'c])
    (uparse? [1 "hello"] [thru "hello"])
    (
        res: 0
        did all [
            uparse? [1 "hello" a 1 2 3 b] [thru "hello" <any> res: across to 'b <any>]
            res = [1 2 3]
        ]
    )
    (not uparse? [] [to 'a])
    (not uparse? [] [to ['a]])
]

[
    "block-modify"

    (error? try [uparse? [] [remove]])
    (not uparse? [] [remove <any>])
    (
        blk: [a]
        did all [
            uparse? blk [remove <any>]
            blk = []
        ]
    )
    (
        blk: [a b a]
        did all [
            uparse? blk [some ['a | remove 'b]]
            blk = [a a]
        ]
    )
    (did all [
        uparse? blk: [] [insert (1)]
        blk = [1]
    ])
    (did all [
        uparse? blk: [a a] [<any> insert ^(the b) <any>]
        blk = [a b a]
    ])
    (did all [
        uparse? blk: [] [p: <here> insert ^(the a) seek (p) remove 'a]
        blk = []
    ])
    (did all [
        uparse? blk: [] [insert ([a b])]
        blk = [a b]
    ])
    (did all [
        uparse? blk: [] [insert ^([a b])]
        blk = [[a b]]
    ])
    (
        series: [a b c]
        letter: 'x
        did all [
            uparse? series [insert ^(letter) 'a 'b 'c]
            series == [x a b c]
        ]
    )
    (
        series: [a b c]
        letters: [x y z]
        did all [
            uparse? series ['a 'b insert (letters) insert ^(letters) 'c]
            series == [a b x y z [x y z] c]
        ]
    )
    (
        series: [a b c]
        letters: [x y z]
        did all [
            uparse? series [
                mark: <here>
                'a

                ; Try equivalent of Red's `insert mark letters`
                pos: <here>
                seek (mark)
                insert (letters)
                after: <here>
                seek (skip pos (index? after) - (index? mark))

                ; Try equivalent of Red's `insert only mark letters`
                pos: <here>
                seek (mark)
                insert ^(letters)
                after: <here>
                seek (skip pos (index? after) - (index? mark))

                'b 'c
            ]
            series == [[x y z] x y z a b c]
        ]
    )
    (
        series: [a b c]
        letter: 'x
        did all [
            uparse? series [
                mark: <here> insert ^(letter) 'a 'b

                ; Try equivalent of Red's `insert only mark letter`
                pos: <here>
                seek (mark)
                insert ^(letter)
                after: <here>
                seek (skip pos (index? after) - (index? mark))

                'c
            ]
            series == [x x a b c]
        ]
    )
    (
        series: [a b c]
        letters: [x y z]
        did all [
            uparse? series [
                to <end> mark: <here> [false]
                |
                ; Try equivalent of Red's `insert only mark letters`
                pos: <here>
                seek (mark)
                insert ^(letters)
                after: <here>
                ; don't adjust pos, it is before insert point
                seek (pos)

                ; Try equivalent of Red's `insert mark letters`
                pos: <here>
                seek (mark)
                insert (letters)
                after: <here>
                ; don't adjust pos, it is before insert point
                seek (pos)

                'a 'b 'c 'x 'y 'z block!
            ]
            series == [a b c x y z [x y z]]
        ]
    )
    (
        series: [b]
        digit: 2
        did all [
            uparse? series [insert (digit) 'b]
            series == [2 b]
        ]
    )
    (did all [
        uparse? blk: [1] [change integer! ^(the a)]
        blk = [a]
    ])
    (did all [
        uparse? blk: [1 2 3] [change [some integer!] ^(the a)]
        blk = [a]
    ])
    (did all [
        uparse? blk: [1 a 2 b 3] [some [change word! (#.) | integer!]]
        blk = [1 #. 2 #. 3]
    ])
    (did all [
        uparse? blk: [1 2 3] [change [some integer!] (99)]
        blk = [99]
    ])
    (did all [
        uparse? blk: [1 2 3] [change [some integer!] ^([a])]
        blk = [[a]]
    ])
    (did all [
        uparse? blk: [1 2 3] [change [some integer!] ^(reduce [1 + 2])]
        blk = [[3]]
    ])
    (
        b: ["long long long string" "long long long string" [1]]
        uparse? copy "." [change <any> (b)]
    )
]

[
    "block-recurse"

    (uparse? [a "test"] ['a s: text! (assert [uparse? s [4 <any>]])])
]

[
    "block-misc"

    (
        wa: ['a]
        wb: ['b]
        true
    )
    (uparse? [] [break])
    (not uparse? [a] [break])
    (uparse? [a] [[break 'b] 'a])
    (uparse? [a] [['b | break] 'a])
    (uparse? [a a] [some ['b | break] 2 'a])
    (uparse? [a a] [some ['b | [break]] 2 'a])
    (not uparse? [a a] [some ['b | 2 ['c | break]] 2 'a])
    (not uparse? [] [false])
    (not uparse? [a] ['a false])
    (not uparse? [a] [[false]])
    (not uparse? [a] [false | false])
    (not uparse? [a] [[false | false]])
    (not uparse? [a] ['b | false])
    (not uparse? [] [not <end>])
    (uparse? [a] [not 'b 'a])
    (not uparse? [a] [not <any>])
    (not uparse? [a] [not <any> <any>])
    (uparse? [a] [not ['b] 'a])
    (uparse? [a] [not wb 'a])
    (not uparse? [a a] [not ['a 'a] to <end>])
    (uparse? [a a] [not [some 'b] to <end>])
    (uparse? [a a] [some further ['c | not 'b] 2 <any>])
    (uparse? [wb] [quote wb])
    (uparse? [123] [quote 123])
    (uparse? [3 3] [2 quote 3])
    (uparse? [blank] [quote blank])
    (uparse? [some] [quote some])
    (not uparse? [] [reject])
    (not uparse? [a] [reject 'a])
    (not uparse? [a] [reject wa])
    (not uparse? [a] [[reject] 'a])
    (uparse? [a] [[reject 'b] | 'a])
    (not uparse? [a] [['b | reject] 'a])
    (uparse? [a] [['b | reject] | 'a])
    (uparse? [a a] [some reject | 2 'a])
    (uparse? [a a] [some [reject] | 2 'a])
    (uparse? [] [blank])
    (uparse? [a] [<any> blank])
    (uparse? [a] [blank <any> blank])
    (uparse? [a] ['a blank])
    (uparse? [a] [blank 'a blank])
    (uparse? [a] [wa blank])
    (uparse? [a] [blank wa blank])
    (uparse? [a] [['b | blank] 'a])
    (uparse? [a] [['b | [blank]] 'a])
    (uparse? [a] [[['b | [blank]]] 'a])
    (uparse? [] [opt blank])
    (uparse? [] [opt 'a])
    (uparse? [a] [opt 'a])
    (uparse? [a] [opt 'b 'a])
    (uparse? [a] [opt ['a]])
    (uparse? [a] [opt wa])
    (uparse? [a] [opt <any>])
    (uparse? [a b c] [<any> opt 'b <any>])
    (uparse? [[]] [into any-series! []])
    (uparse? [[a]] [into any-series! ['a]])
    (uparse? [b [a] c] ['b into any-series! ['a] 'c])
    (uparse? ["a"] [into any-series! [#a]])
    (uparse? [b "a" c] ['b into any-series! ["a"] 'c])
    (uparse? [["a"]] [into block! [into any-series! [#a]]])
    (not uparse? [[a]] [into any-series! ['a 'b]])
    (not uparse? [[a]] [into any-series! [some 'b]])
    (uparse? [[a]] [into any-series! ['a 'b] | block!])
]

[
    (
        x: blank
        true
    )
    (uparse? [2 4 6] [while [x: integer! :(even? x)]])
    (not uparse? [1] [x: integer! :(even? x)])
    (not uparse? [1 5] [some [x: integer! :(even? x)]])
    (uparse? [] [while 'a])
    (uparse? [] [while 'b])
    (uparse? [a] [while 'a])
    (not uparse? [a] [while 'b])
    (uparse? [a] [while 'b <any>])
    (uparse? [a b a b] [while ['b | 'a]])
    (error? try [uparse? [] [ahead]])
    (uparse? [a] [ahead 'a 'a])
    (uparse? [1] [ahead [block! | integer!] <any>])
]

[
    "block-part"

    (
        input: [h 5 #l "l" o]
        input2: [a a a b b]
        true
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any>] 3
            v = [h 5 #l]
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] 4
            v = [h 5 #l]
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any> <any>] 4
            v = [h 5 #l]
        ]
    )
    (
        v: blank
        did all [
            uparse?/part next input [v: across 3 <any>] 3
            v = [5 #l "l"]
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across to 'o <any>] 3
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across to 'o <any>] 5
            v = [h 5 #l "l"]
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input2 [v: across 3 'a] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input2 [v: across 3 'a] 3
            v = [a a a]
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] skip input 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any>] skip input 3
            v = [h 5 #l]
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] skip input 4
            v = [h 5 #l]
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any> <any>] skip input 4
            v = [h 5 #l]
        ]
    )
    (
        v: blank
        did all [
            uparse?/part next input [v: across 3 <any>] skip input 4
            v = [5 #l "l"]
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across to 'o <any>] skip input 3
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across to 'o <any>] skip input 5
            v = [h 5 #l "l"]
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input2 [v: across 3 'a] skip input2 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input2 [v: across 3 'a] skip input2 3
            v = [a a a]
        ]
    )
]

[
    https://github.com/red/red/issues/562

    (not uparse? [+] [while ['+ :(no)]])
]

[
    https://github.com/red/red/issues/564

    (not uparse? [a] [0 <any>])
    (uparse? [a] [0 <any> 'a])
    (
        z: blank
        did all [
            not uparse? [a] [z: across 0 <any>]
            z = []
        ]
    )
]

[
    "blk-integer-bug (unlabeled)"
    (uparse? [1 2] [1 2 integer!])
]

[
    "string"

    (uparse? "" [])
    (uparse? "a" [#a])
    (uparse? "a" ["a"])
    (not uparse? "a" [#b])
    (uparse? "ab" [#a #b])
    (uparse? "ab" ["ab"])
    (uparse? "a" [[#a]])
    (uparse? "ab" [[#a] "b"])
    (uparse? "ab" [#a [#b]])
    (uparse? "ab" [[#a] [#b]])
    (uparse? "a" [#b | #a])
    (not uparse? "ab" [#b | "a"])
    (uparse? "a" [[#b | #a]])
    (not uparse? "ab" [[#b | "a"]])
    (uparse? "ab" [["a" | #b] [#b | "a"]])
    (error? try [uparse? "123" [integer!]])
    (
        res: 0
        did all [
            uparse? "" [(res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [#a (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? "a" [#b (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? "" [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [[#a (res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? "a" [[#b (res: 1)]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? "ab" [#a (res: 1) [#c (res: 2) | #b (res: 3)]]
            res = 3
        ]
    )
    (
        res: 0
        did all [
            not uparse? "ab" [#a (res: 1) [#c (res: 2) | #d (res: 3)]]
            res = 1
        ]
    )
    (not uparse? "aa" [1 [#a]])
    (uparse? "aa" [2 [#a]])
    (not uparse? "aa" [3 [#a]])
    (not uparse? "aa" [1 1 [#a]])
    (uparse? "aa" [1 2 [#a]])
    (uparse? "aa" [2 2 [#a]])
    (uparse? "aa" [2 3 [#a]])
    (not uparse? "aa" [3 4 [#a]])
    (not uparse? "aa" [1 #a])
    (uparse? "aa" [2 #a])
    (not uparse? "aa" [3 #a])
    (not uparse? "aa" [1 1 #a])
    (uparse? "aa" [1 2 #a])
    (uparse? "aa" [2 2 #a])
    (uparse? "aa" [2 3 #a])
    (not uparse? "aa" [3 4 #a])
    (not uparse? "aa" [1 <any>])
    (uparse? "aa" [2 <any>])
    (not uparse? "aa" [3 <any>])
    (not uparse? "aa" [1 1 <any>])
    (uparse? "aa" [1 2 <any>])
    (uparse? "aa" [2 2 <any>])
    (uparse? "aa" [2 3 <any>])
    (not uparse? "aa" [3 4 <any>])
    (uparse? "a" [<any>])
    (uparse? "ab" [<any> <any>])
    (uparse? "ab" [<any> [<any>]])
    (uparse? "ab" [[<any>] [<any>]])
    (uparse? "aa" [some [#a]])
    (not uparse? "aa" [some [#a] #b])
    (uparse? "aababbba" [some [<any>]])
    (uparse? "aababbba" [some ["a" | "b"]])
    (not uparse? "aababbba" [some ["a" | #c]])
    (uparse? "aa" [while [#a]])
    (uparse? "aa" [some [#a] while [#b]])
    (uparse? "aabb" [2 #a 2 "b"])
    (not uparse? "aabb" [2 "a" 3 #b])
    (uparse? "aabb" [some #a some "b"])
    (not uparse? "aabb" [some "a" some #c])
    (
        p: blank
        did all [
            uparse? "" [p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? "" [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? "a" [p: <here> #a]
            p = "a"
        ]
    )
    (
        p: blank
        did all [
            uparse? "a" [#a p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? "a" [#a [p: <here>]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            not uparse? "ab" [#a p: <here>]
            p = "b"
        ]
    )
    (
        p: blank
        did all [
            uparse? "ab" [#a [p: <here>] [#b | #c]]
            p = "b"
        ]
    )
    (
        p: blank
        did all [
            uparse? "aaabb" [3 #a p: <here> 2 #b seek (p) [2 "b"]]
            p = "bb"
        ]
    )
    (uparse? "baaac" [<any> some [#a] #c])
]

[
    "string-end"

    (uparse? "a" [#a <end>])
    (not uparse? "ab" [#a <end>])
    (uparse? "a" [<any> <end>])
    (not uparse? "ab" [<any> <end>])
    (uparse? "" [<end>])
    (
        be6: 0
        did all [
            uparse? "" [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

[
    "string-words"

    (
        wa: [#a]
        wb: [#b]
        wca: #a
        wcb: #b
        wra: [wa]
        wrb: [wb]
        wh: "hello"
        wrab: [#a | #b]
        wrba: [#b | #a]
        true
    )
    (uparse? "a" [wa])
    (not uparse? "a" [wb])
    (uparse? "ab" [wa wb])
    (uparse? "a" [wra])
    (uparse? "ab" [wra #b])
    (uparse? "ab" [#a wrb])
    (uparse? "ab" [wra wrb])
    (uparse? "hello" [wh])
    (uparse? "a" [wcb | wca])
    (not uparse? "ab" [wb | wa])
    (uparse? "a" [[wcb | wca]])
    (not uparse? "ab" [wrba])
    (uparse? "ab" [wrab wrba])
    (
        res: 0
        did all [
            uparse? "a" [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? "a" [wb (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        wres: [(res: 1)]
        did all [
            uparse? "" [wres]
            res = 1
        ]
    )
    (
        res: 0
        wres: [#a (res: 1)]
        did all [
            uparse? "a" [wres]
            res = 1
        ]
    )
    (
        res: 0
        wres: [#b (res: 1)]
        did all [
            not uparse? "a" [wres]
            res = 0
        ]
    )
]

[
    "string-extraction"

    (
        wa: [#a]
        true
    )
    (
        res: 0
        did all [
            uparse? "a" [res: across <any>]
            res = "a"
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [res: across #a]
            res = "a"
        ]
    )
    (
        res: 0
        res2: 0
        did all [
            uparse? "a" [res: across res2: across #a]
            res = "a"
            res2 = "a"
        ]
    )
    (
        res: 0
        did all [
            uparse? "aa" [res: across 2 #a]
            res = "aa"
        ]
    )
    (
        res: 0
        did all [
            not uparse? "aa" [res: across 3 #a]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [res: across [#a]]
            res = "a"
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [res: across wa]
            res = "a"
        ]
    )
    (
        res: 0
        did all [
            uparse? "aa" [res: across 2 wa]
            res = "aa"
        ]
    )
    (
        res: 0
        did all [
            uparse? "aab" [<any> res: across #a <any>]
            res = "a"
        ]
    )
    (
        res: 0
        did all [
            uparse? "aab" [<any> res: across [#a | #b] <any>]
            res = "a"
        ]
    )
    (
        res: 0
        did all [
            not uparse? "a" [res: across [#c | #b]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [res: <any>]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [res: #a]
            res = #a
        ]
    )
    (
        res: 0
        res2: 0
        did all [
            uparse? "a" [res: res2: #a]
            res = #a
            res2 = #a
        ]
    )
    (
        res: 0
        did all [
            uparse? "aa" [res: 2 #a]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            not uparse? "aa" [res: 3 #a]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [res: [#a]]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            uparse? "a" [res: wa]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            uparse? "aa" [res: 2 wa]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            uparse? "aab" [<any> res: #a <any>]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            uparse? "aab" [<any> res: [#a | #b] <any>]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            not uparse? "a" [res: [#c | #b]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? "baaac" [<any> res: some #a #c]
            res = #a
        ]
    )
    (
        res: 0
        did all [
            uparse? "baaac" [<any> res: some wa #c]
            res = #a
        ]
    )
    (
        res: uparse? "" [collect []]
        res = []
    )
    (
        res: uparse? "1" [collect []]
        res = []
    )
    (
        res: uparse? "1" [collect [keep <any>]]
        res = [#1]
    )
    (
        digit: charset "0123456789"
        res: uparse? "123" [collect [some [keep digit]]]
        res = [#1 #2 #3]
    )
    (
        res: uparse? "123" [
            collect [some [
                keep [v: across digit :(even? load-value v)]
                |
                <any>
            ]]
        ]
        res = [#2]
    )
    (
        res: uparse? "123" [collect [some [d: across digit keep (load d)]]]
        res = [1 2 3]
    )
    (
        a: blank
        did all [
            uparse? "" [a: collect []]
            a = []
        ]
    )
    (
        a: blank
        did all [
            uparse? "1" [a: collect [keep <any>]]
            a = [#1]
        ]
    )
    (
        res: uparse? "aabbb" [collect [keep some "a" keep some #b]]
        res = ["aa" "bbb"]
    )
    (
        alpha: charset [#a - #z]
        res: uparse? "abc|def" [collect [while [keep some alpha | <any>]]]
        res = ["abc" "def"]
    )
]

[
    "#1093"
    (
        se53-copied: copy ""
        did all [
            uparse? "abcde" ["xyz" | s: across to <end> (se53-copied: :s)]
            "abcde" = se53-copied
        ]
    )
]

[
    "string-skipping"

    (
        str: "Lorem ipsum dolor sit amet."
        wa: [#a]
        true
    )
    (uparse? "" [to <end>])
    (uparse? "" [thru <end>])
    (uparse? "a" [to <end>])
    (not uparse? "a" [to #a])
    (not uparse? "a" [to #a <end>])
    (uparse? "a" [to #a <any>])
    (uparse? "a" [thru #a])
    (uparse? "a" [thru #a <end>])
    (not uparse? "a" [thru #a <any>])
    (uparse? "ab" [to #a 2 <any>])
    (uparse? "ab" [thru #a <any>])
    (uparse? "aaab" [to #a to <end>])
    (uparse? "aaba" [<any> thru #a 2 <any>])
    (not uparse? "a" [to [#a]])
    (not uparse? "a" [to [#a] <end>])
    (uparse? "a" [to [#a] <any>])
    (uparse? "a" [thru [#a]])
    (uparse? "a" [thru [#a] <end>])
    (not uparse? "a" [thru [#a] <any>])
    (uparse? "ab" [to [#a] 2 <any>])
    (uparse? "ab" [thru [#a] <any>])
    (uparse? "aaab" [to [#a] to <end>])
    (uparse? "aaba" [<any> thru [#a] 2 <any>])
    (uparse? "zzabc" [to [#c | #b | #a] 3 <any>])
    (uparse? "zzabc" [to [#a | #b | #c] 3 <any>])
    (uparse? "zzabc" [thru [#c | #b | #a] 2 <any>])
    (uparse? "zzabc" [thru [#a | #b | #c] 2 <any>])
    (uparse? "bbaaac" [thru 3 #a #c])
    (uparse? "bbaaac" [thru 3 "a" "c"])
    (uparse? "bbaaac" [thru 3 wa #c])
    (uparse? "bbaaac" [thru [3 "a"] "c"])
    (uparse? "bbaaac" [thru some "a" "c"])
    (uparse? "bbaaac" [thru [some #a] "c"])
    (uparse? "bbaaac" [thru [some #x | "aaa"] "c"])
    (uparse? str [thru "amet" <any>])
    (
        res: 0
        did all [
            uparse? str [thru "ipsum" <any> res: across to #" " to <end>]
            res = "dolor"
        ]
    )
    (
        res: 0
        did all [
            uparse? str [thru #p res: to <end>]
            9 = index? res
        ]
    )
    (not uparse? "" [to "a"])
    (not uparse? "" [to #a])
    (not uparse? "" [to ["a"]])
    (not uparse? "" [to [#a]])
]

[
    "string-casing"

    (uparse? "a" ["A"])
    (uparse? "a" [#A])
    (not uparse?/case "a" ["A"])
    (not uparse?/case "a" [#A])
    (uparse?/case "a" ["a"])
    (uparse?/case "a" [#a])
    (uparse?/case "A" ["A"])
    (uparse?/case "A" [#A])
    (uparse? "TeSt" ["test"])
    (not uparse?/case "TeSt" ["test"])
    (uparse?/case "TeSt" ["TeSt"])
]

[
    "string-unicode"

    (uparse? "abcdé" [#a #b #c #d #é])
    (uparse? "abcdé" ["abcdé"])
    (not uparse? "abcde" [#a #b #c #d #é])
    (uparse? "abcdé" [#a #b #c #d #é])
    (uparse? "abcdé✐" [#a #b #c #d #é #"✐"])
    (uparse? "abcdé✐" ["abcdé✐"])
    (not uparse? "abcdé" ["abcdé✐"])
    (not uparse? "ab✐cdé" ["abcdé✐"])
    (not uparse? "abcdé✐" ["abcdé"])
    (uparse? "✐abcdé" ["✐abcdé"])
    (uparse? "abcdé✐𐀀" [#a #b #c #d #é #"✐" #"𐀀"])
    (uparse? "ab𐀀cdé✐" ["ab𐀀cdé✐"])
    (not uparse? "abcdé" ["abc𐀀dé"])
    (not uparse? "𐀀abcdé" ["a𐀀bcdé"])
    (not uparse? "abcdé𐀀" ["abcdé"])
    (uparse? "𐀀abcdé" ["𐀀abcdé"])
]

[
    "string-bitsets"

    (
        bs: charset ["hello" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (uparse? "abc" [some bs])
    (not uparse? "123" [some bs])
    (not uparse? "ABC" [some bs])
    (uparse? "abc" [some [bs]])
    (not uparse? "123" [some [bs]])
    (uparse? "abc" [some wbs])
    (not uparse? "123" [some wbs])
    (uparse? "abc" [some wbs2])
    (not uparse? "123" [some wbs2])
    (uparse? "abc" [bs bs bs])
    (not uparse? "123" [bs bs bs])
    (uparse? "abc" [[bs] [bs] [bs]])
    (not uparse? "123" [[bs] [bs] [bs]])
    (uparse? "abc" [wbs wbs wbs])
    (not uparse? "123" [wbs wbs wbs])
    (uparse? "abc" [wbs2 wbs2 wbs2])
    (not uparse? "123" [wbs2 wbs2 wbs2])
]

[
    (
        bs: charset [not "hello123" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (not uparse? "abc" [some bs])
    (uparse? "ABC" [some bs])
    (not uparse? "123" [some bs])
    (uparse? "789" [some bs])
    (not uparse? "abc" [bs bs bs])
    (uparse? "ABC" [bs bs bs])
    (not uparse? "123" [bs bs bs])
    (uparse? "789" [bs bs bs])
    (
        digit: charset "0123456789"
        did all [
            uparse? "hello 123" [to digit p: 3 <any>]
            p = "123"
        ]
    )
]

[
    "string-modify"

    (
        ws: charset " ^- ^/^M"
        not-ws: complement ws
        true
    )
    (error? try [uparse? "" [remove]])
    (not uparse? "" [remove <any>])
    (
        str: "a"
        did all [
            uparse? str [remove <any>]
            str = ""
        ]
    )
    (
        str: "aba"
        did all [
            uparse? str [some [#a | remove #b]]
            str = "aa"
        ]
    )
    (
        str: "hello world"
        did all [
            uparse? str [remove thru ws "world"]
            str = "world"
        ]
    )
    (
        str: "hello world"
        did all [
            uparse? str [remove "hello" <any> "world"]
            str = " world"
        ]
    )
    (did all [
        uparse? s: " t e s t " [while [remove ws | <any>]]
        s = "test"
    ])
    (did all [
        uparse? s: " t e s t " [while [remove ws | <any>]]
        s = "test"
    ])
    (
        str: "hello 123 world"
        digit: charset "0123456789"
        did all [
            uparse? str [while [remove [some digit #" "] | <any>]]
            str = "hello world"
        ]
    )
    (did all [
        uparse? str: "" [insert (#1)]
        str = "1"
    ])
    (did all [
        uparse? str: "aa" [<any> insert (#b) <any>]
        str = "aba"
    ])
    (did all [
        uparse? str: "" [p: <here> insert (#a) seek (p) remove #a]
        str = ""
    ])
    (did all [
        uparse? str: "test" [
            some [<any> p: <here> insert (#_)] seek (p) remove <any>
        ]
        str = "t_e_s_t"
    ])
    (did all [
        uparse? str: "1" [change <any> (#a)]
        str = "a"
    ])
    (did all [
        uparse? str: "123" [change [3 <any>] (#a)]
        str = "a"
    ])
    (
        alpha: charset [#a - #z]
        did all [
            uparse? str: "1a2b3" [some [change alpha (#.) | <any>]]
            str = "1.2.3"
        ]
    )
    (did all [
        uparse? str: "123" [change 3 <any> (99)]
        str = "99"
    ])
    (did all [
        uparse? str: "test" [some [change #t (#o) | <any>]]
        str = "oeso"
    ])
    (did all [
        uparse? str: "12abc34" [
            some [to alpha change [some alpha] ("zzzz")] 2 <any>
        ]
        str = "12zzzz34"
    ])
]

[
    "string-misc"

    (
        wa: [#a]
        wb: [#b]
        true
    )
    (uparse? "" [break])
    (not uparse? "a" [break])
    (uparse? "a" [[break #b] #a])
    (uparse? "a" [[#b | break] #a])
    (uparse? "aa" [some [#b | break] 2 #a])
    (uparse? "aa" [some [#b | [break]] 2 #a])
    (not uparse? "aa" [some [#b | 2 [#c | break]] 2 #a])
    (not uparse? "" [false])
    (not uparse? "a" [#a false])
    (not uparse? "a" [[false]])
    (not uparse? "a" [false | false])
    (not uparse? "a" [[false | false]])
    (not uparse? "a" [#b | false])
    (not uparse? "" [not <end>])
    (uparse? "a" [not #b #a])
    (not uparse? "a" [not <any>])
    (not uparse? "a" [not <any> <any>])
    (uparse? "a" [not [#b] #a])
    (uparse? "a" [not wb #a])
    (not uparse? "aa" [not [#a #a] to <end>])
    (uparse? "aa" [not [some #b] to <end>])
    (uparse? "aa" [some further [#c | not #b] 2 <any>])
    (not uparse? "" [reject])
    (not uparse? "a" [reject #a])
    (not uparse? "a" [reject wa])
    (not uparse? "a" [[reject] #a])
    (uparse? "a" [[reject #b] | #a])
    (not uparse? "a" [[#b | reject] #a])
    (uparse? "a" [[#b | reject] | #a])
    (uparse? "aa" [some reject | 2 #a])
    (uparse? "aa" [some [reject] | 2 #a])
    (uparse? "" [blank])
    (uparse? "a" [<any> blank])
    (uparse? "a" [blank <any> blank])
    (uparse? "a" [#a blank])
    (uparse? "a" [blank #a blank])
    (uparse? "a" [wa blank])
    (uparse? "a" [blank wa blank])
    (uparse? "a" [[#b | blank] #a])
    (uparse? "a" [[#b | [blank]] #a])
    (uparse? "a" [[[#b | [blank]]] #a])
    (uparse? "" [opt blank])
    (uparse? "" [opt #a])
    (uparse? "a" [opt #a])
    (uparse? "a" [opt #b #a])
    (uparse? "a" [opt [#a]])
    (uparse? "a" [opt wa])
    (uparse? "a" [opt <any>])
    (uparse? "abc" [<any> opt #b <any>])
]

[
    (
        x: blank
        true
    )
    (uparse? "246" [while [x: across <any> :(even? load-value x)]])
    (not uparse? "1" [x: across <any> :(even? load-value x)])
    (not uparse? "15" [some [x: across <any> :(even? load-value x)]])
    (uparse? "" [while #a])
    (uparse? "" [while #b])
    (uparse? "a" [while #a])
    (not uparse? "a" [while #b])
    (uparse? "a" [while #b <any>])
    (uparse? "abab" [while [#b | #a]])
    (error? try [uparse? "" [ahead]])
    (uparse? "a" [ahead #a #a])
    (uparse? "1" [ahead [#a | #1] <any>])
]

[
    "string-part"

    (
        input: "hello"
        input2: "aaabb"
        letters: charset [#a - #o]
        true
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any>] 3
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] 4
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any> <any>] 4
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            uparse?/part next input [v: across 3 <any>] 3
            v = "ell"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across to #o <any>] 3
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across to #o <any>] 5
            v = "hell"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 letters] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 letters] 3
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input2 [v: across 3 #a] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input2 [v: across 3 #a] 3
            v = "aaa"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] skip input 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any>] skip input 3
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] skip input 4
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any> <any>] skip input 4
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            uparse?/part next input [v: across 3 <any>] skip input 4
            v = "ell"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across to #o <any>] skip input 3
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across to #o <any>] skip input 5
            v = "hell"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 letters] skip input 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 letters] skip input 3
            v = "hel"
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input2 [v: across 3 #a] skip input2 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input2 [v: across 3 #a] skip input2 3
            v = "aaa"
        ]
    )
]

[
    "expression parser"
    (
        expr: [term ["+" | "-"] expr | term]
        term: [factor ["*" | "/"] term | factor]
        factor: [primary "**" factor | primary]
        primary: [some digit | "(" expr ")"]
        digit: charset "0123456789"
        true
    )

    (not uparse? "123a+2" expr)
    (not uparse? "a+b" expr)
    (uparse? "4/5+3**2-(5*6+1)" expr)
    (uparse? "1+2*(3-2)/4" expr)
    (not uparse? "(+)" expr)
    (uparse? "(1*9)" expr)
    (uparse? "(1)" expr)
    (uparse? "1+2*(3-2)/4" expr)
    (not uparse? "1+2*(3-2)/" expr)
    (uparse? "1+2*(3-2)" expr)
    (not uparse? "1+2*(3-2" expr)
    (not uparse? "1+2*(3-" expr)
    (not uparse? "1+2*(3" expr)
    (not uparse? "1+2*(" expr)
    (not uparse? "1+2*" expr)
    (uparse? "1+2" expr)
    (not uparse? "1+" expr)
    (uparse? "1" expr)
]

[
    "recursive html parser (uses RULE inside RULE)"
    (
        html: {
^-^-^-<html>
^-^-^-^-<head><title>Test</title></head>
^-^-^-^-<body><div><u>Hello</u> <b>World</b></div></body>
^-^-^-</html>
^-^-}
        ws: charset " ^- ^/^M"
        res: uparse html rule: [
            collect [while [
                ws
                | "</" thru ">" break
                | "<" name: across to ">" <any> keep ^(load-value name) opt rule
                | str: across to "<" keep (str)
            ]]
        ]
        res = [html [head [title ["Test"]] body [div [u ["Hello"] b ["World"]]]]]
    )
]

[
    (
        foo: func [value] [value]
        res: uparse [a 3 4 t [t 9] "test" 8] [
            collect [
                while [
                    keep integer!
                    | p: <here> block! seek (p) into any-series! [
                        keep ^ collect [while [
                            keep integer! keep ^('+)
                            | <any> keep ^(foo '-)
                        ]]
                    ]
                    | <any>
                ]
            ]
        ]
        res = [3 4 [- 9 +] 8]
    )
]

[
    ; taken from http://www.rebol.net/wiki/Parse_Project#AND
    (
        nanb: [#a opt nanb #b]
        nbnc: [#b opt nbnc #c]
        nanbnc: [ahead [nanb #c] some #a nbnc]
        did all [
            uparse? "abc" nanbnc
            uparse? "aabbcc" nanbnc
            uparse? "aaabbbccc" nanbnc
            not uparse? "abbc" nanbnc
            not uparse? "abcc" nanbnc
            not uparse? "aabbc" nanbnc
        ]
    )
]

[
    (
        split-test5: func [series [text!] dlm [text! char!] <local> value] [
            rule: complement charset dlm
            uparse series [collect [while [keep value: across some rule | <any>]]]
        ]
        true
    )
    (["Hello" "bright" "world!"] = split-test5 "Hello bright world!" space)
    (["Hell" "bright" "w" "rld!"] = split-test5 "Hello bright world!" " o")
]

[
    https://github.com/red/red/issues/562

    (not uparse? "+" [while [#+ :(no)]])
]

[
    https://github.com/red/red/issues/564

    (not uparse? "a" [0 <any>])
    (uparse? "a" [0 <any> #a])
    (
        z: blank
        did all [
            not uparse? "a" [z: across 0 <any>]
            z = ""
        ]
    )
    (
        f: func [
            s [text!]
        ] [
            r: [
                l: across <any> (l: load-value l)
                x: across repeat (l) <any>
                [
                    #","
                    | #"]" :(f x)
                ]
            ]
            uparse? s [while r <end>]
        ]
        f "420,]]"
    )
]

[
    https://github.com/red/red/issues/563

    (
        f563: func [t [text!]] [uparse? t [while r]]

        r: [#+, :(res: f563 "-", assert [not res], res)]

        did all [
            not f563 "-"
            not f563 "+"
        ]
    )
]

[
    https://github.com/red/red/issues/567

    (
        res: uparse? "12" [collect [keep value: across 2 <any>]]
        res = ["12"]
    )
]

[
    https://github.com/red/red/issues/569

    (
        size: 1
        res: uparse? "1" [collect [keep value: across (size) <any>]]
        res = ["1"]
    )(
        size: 2
        res: uparse? "12" [collect [keep value: across (size) <any>]]
        res = ["12"]
    )
]

[
    https://github.com/red/red/issues/678

    (uparse? "catcatcatcat" [4 "cat"])
    (uparse? "catcatcat" [3 "cat"])
    (uparse? "catcat" [2 "cat"])
    (not uparse? "cat" [4 "cat"])
    (not uparse? "cat" [3 "cat"])
    (not uparse? "cat" [2 "cat"])
    (uparse? "cat" [1 "cat"])
]

[
    https://github.com/red/red/issues/748
    (
        txt: "Hello world"
        uparse? txt [while [while [remove "l" | <any>] false]]
        did all [
            txt = "Heo word"
            8 = length? txt
        ]
    )
]

[
    "binary"

    (uparse? #{} [])
    (uparse? #{0A} [#{0A}])
    (uparse? #{0A} [#"^/"])
    (not uparse? #{0A} [#{0B}])
    (uparse? #{0A0B} [#{0A} #{0B}])
    (uparse? #{0A0B} [#{0A0B}])
    (uparse? #{0A} [[#{0A}]])
    (uparse? #{0A0B} [[#{0A}] #{0B}])
    (uparse? #{0A0B} [#{0A} [#{0B}]])
    (uparse? #{0A0B} [[#{0A}] [#{0B}]])
    (uparse? #{0A} [#{0B} | #{0A}])
    (not uparse? #{0A0B} [#{0B} | #{0A}])
    (uparse? #{0A} [[#{0B} | #{0A}]])
    (not uparse? #{0A0B} [[#{0B} | #{0A}]])
    (uparse? #{0A0B} [[#{0A} | #{0B}] [#{0B} | #{0A}]])
    (
        res: 0
        did all [
            uparse? #{} [(res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [#{0A} (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A} [#{0B} (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{} [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [[#{0A} (res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A} [[#{0B} (res: 1)]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0B} [#{0A} (res: 1) [#"^L" (res: 2) | #{0B} (res: 3)]]
            res = 3
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A0B} [#{0A} (res: 1) [#{0C} (res: 2) | #{0D} (res: 3)]]
            res = 1
        ]
    )
    (not uparse? #{0A0A} [1 [#{0A}]])
    (uparse? #{0A0A} [2 [#{0A}]])
    (not uparse? #{0A0A} [3 [#{0A}]])
    (not uparse? #{0A0A} [1 1 [#{0A}]])
    (uparse? #{0A0A} [1 2 [#{0A}]])
    (uparse? #{0A0A} [2 2 [#{0A}]])
    (uparse? #{0A0A} [2 3 [#{0A}]])
    (not uparse? #{0A0A} [3 4 [#{0A}]])
    (not uparse? #{0A0A} [1 #{0A}])
    (uparse? #{0A0A} [2 #{0A}])
    (not uparse? #{0A0A} [3 #{0A}])
    (not uparse? #{0A0A} [1 1 #{0A}])
    (uparse? #{0A0A} [1 2 #{0A}])
    (uparse? #{0A0A} [2 2 #{0A}])
    (uparse? #{0A0A} [2 3 #{0A}])
    (not uparse? #{0A0A} [3 4 #{0A}])
    (not uparse? #{0A0A} [1 <any>])
    (uparse? #{0A0A} [2 <any>])
    (not uparse? #{0A0A} [3 <any>])
    (not uparse? #{0A0A} [1 1 <any>])
    (uparse? #{0A0A} [1 2 <any>])
    (uparse? #{0A0A} [2 2 <any>])
    (uparse? #{0A0A} [2 3 <any>])
    (not uparse? #{0A0A} [3 4 <any>])
    (uparse? #{0A} [<any>])
    (uparse? #{0A0B} [<any> <any>])
    (uparse? #{0A0B} [<any> [<any>]])
    (uparse? #{0A0B} [[<any>] [<any>]])
    (uparse? #{0A0A} [some [#{0A}]])
    (not uparse? #{0A0A} [some [#{0A}] #{0B}])
    (uparse? #{0A0A0B0A0B0B0B0A} [some [<any>]])
    (uparse? #{0A0A0B0A0B0B0B0A} [some [#{0A} | #{0B}]])
    (not uparse? #{0A0A0B0A0B0B0B0A} [some [#{0A} | #"^L"]])
    (uparse? #{0A0A} [while [#{0A}]])
    (uparse? #{0A0A} [some [#{0A}] while [#{0B}]])
    (uparse? #{0A0A0B0B} [2 #{0A} 2 #{0B}])
    (not uparse? #{0A0A0B0B} [2 #{0A} 3 #{0B}])
    (uparse? #{0A0A0B0B} [some #{0A} some #{0B}])
    (not uparse? #{0A0A0B0B} [some #{0A} some #"^L"])
    (
        p: blank
        did all [
            uparse? #{} [p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? #{} [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? #{0A} [p: <here> #{0A}]
            p = #{0A}
        ]
    )
    (
        p: blank
        did all [
            uparse? #{0A} [#{0A} p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? #{0A} [#{0A} [p: <here>]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            not uparse? #{0A0B} [#{0A} p: <here>]
            p = #{0B}
        ]
    )
    (
        p: blank
        did all [
            uparse? #{0A0B} [#{0A} [p: <here>] [#{0B} | #"^L"]]
            p = #{0B}
        ]
    )
    (
        p: blank
        did all [
            uparse? #{0A0A0A0B0B} [3 #{0A} p: <here> 2 #{0B} seek (p) [2 #{0B}]]
            p = #{0B0B}
        ]
    )
    (uparse? #{0B0A0A0A0C} [<any> some [#{0A}] #"^L"])
]

[
    "binary-end"

    (uparse? #{0A} [#{0A} <end>])
    (not uparse? #{0A0B} [#{0A} <end>])
    (uparse? #{0A} [<any> <end>])
    (not uparse? #{0A0B} [<any> <end>])
    (uparse? #{} [<end>])
    (
        be6: 0
        did all [
            uparse? #{} [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

[
    "binary-words"

    (
        wa: [#{0A}]
        wb: [#{0B}]
        wca: #{0A}
        wcb: #{0B}
        wra: [wa]
        wrb: [wb]
        wh: #{88031100}
        wrab: [#{0A} | #{0B}]
        wrba: [#{0B} | #{0A}]
    )
    (uparse? #{0A} [wa])
    (not uparse? #{0A} [wb])
    (uparse? #{0A0B} [wa wb])
    (uparse? #{0A} [wra])
    (uparse? #{0A0B} [wra #{0B}])
    (uparse? #{0A0B} [#{0A} wrb])
    (uparse? #{0A0B} [wra wrb])
    (uparse? #{88031100} [wh])
    (uparse? #{0A} [wcb | wca])
    (not uparse? #{0A0B} [wb | wa])
    (uparse? #{0A} [[wcb | wca]])
    (not uparse? #{0A0B} [wrba])
    (uparse? #{0A0B} [wrab wrba])
    (
        res: 0
        did all [
            uparse? #{0A} [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A} [wb (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        wres: [(res: 1)]
        did all [
            uparse? #{} [wres]
            res = 1
        ]
    )
    (
        res: 0
        wres: [#{0A} (res: 1)]
        did all [
            uparse? #{0A} [wres]
            res = 1
        ]
    )
    (
        res: 0
        wres: [#{0B} (res: 1)]
        did all [
            not uparse? #{0A} [wres]
            res = 0
        ]
    )
]

[
    "binary-extraction"
    (
        wa: [#{0A}]
        true
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: across <any>]
            res = #{0A}
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: across #{0A}]
            res = #{0A}
        ]
    )
    (
        res: 0
        res2: 0
        did all [
            uparse? #{0A} [res: across res2: across #{0A}]
            res = #{0A}
            res2 = #{0A}
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A} [res: across 2 #{0A}]
            res = #{0A0A}
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A0A} [res: across 3 #{0A}]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: across [#{0A}]]
            res = #{0A}
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: across wa]
            res = #{0A}
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A} [res: across 2 wa]
            res = #{0A0A}
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A0B} [<any> res: across #{0A} <any>]
            res = #{0A}
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A0B} [<any> res: across [#{0A} | #{0B}] <any>]
            res = #{0A}
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A} [res: across [#"^L" | #{0B}]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: <any>]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: #{0A}]
            res = 10
        ]
    )
    (
        res: 0
        res2: 0
        did all [
            uparse? #{0A} [res: res2: #{0A}]
            res = 10
            res2 = 10
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A} [res: 2 #{0A}]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A0A} [res: 3 #{0A}]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: [#{0A}]]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [res: wa]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A} [res: 2 wa]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A0B} [<any> res: #{0A} <any>]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0A0B} [<any> res: [#{0A} | #{0B}] <any>]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A} [res: [#"^L" | #{0B}]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0B0A0A0A0C} [<any> res: some #{0A} #"^L"]
            res = 10
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0B0A0A0A0C} [<any> res: some wa #"^L"]
            res = 10
        ]
    )
    (
        res: uparse? #{} [collect []]
        res = []
    )
    (
        res: uparse? #{01} [collect []]
        res = []
    )
    (
        res: uparse? #{01} [collect [keep <any>]]
        res = [1]
    )
    (
        digit: charset [0 - 9]
        res: uparse? #{010203} [collect [some [keep digit]]]
        res = [1 2 3]
    )
    (
        res: uparse? #{010203} [
            collect [some [keep [v: across digit :(even? first v)] | <any>]]
        ]
        res = [2]
    )
    (
        res: uparse? #{010203} [collect [some [d: across digit keep (1 + first d)]]]
        res = [2 3 4]
    )
    (
        a: blank
        did all [
            uparse? #{} [a: collect []]
            a = []
        ]
    )
    (
        a: blank
        did all [
            uparse? #{01} [a: collect [keep <any>]]
            a = [1]
        ]
    )
    (
        res: uparse? #{0A0A0B0B0B} [collect [keep some #{0A} keep some #{0B}]]
        res = [#{0A0A} #{0B0B0B}]
    )
    (
        digit: charset [0 - 9]
        res: uparse? #{01020311040506} [collect [while [keep some digit | <any>]]]
        res = [#{010203} #{040506}]
    )
]

[
    https://github.com/red/red/issues/1093

    (
        se53-copied: copy #{}
        did all [
            uparse? #{0102030405} [#{AABBCC} | s: across to <end> (se53-copied: :s)]
            #{0102030405} = se53-copied
        ]
    )
]

[
    "binary-skipping"

    (
        bin: #{0BAD00CAFE00BABE00DEADBEEF00}
        wa: [#{0A}]
        true
    )
    (uparse? #{} [to <end>])
    (uparse? #{} [thru <end>])
    (uparse? #{0A} [to <end>])
    (not uparse? #{0A} [to #{0A}])
    (not uparse? #{0A} [to #{0A} <end>])
    (uparse? #{0A} [to #{0A} <any>])
    (uparse? #{0A} [thru #{0A}])
    (uparse? #{0A} [thru #{0A} <end>])
    (not uparse? #{0A} [thru #{0A} <any>])
    (uparse? #{0A0B} [to #{0A} 2 <any>])
    (uparse? #{0A0B} [thru #{0A} <any>])
    (uparse? #{0A0A0A0B} [to #{0A} to <end>])
    (uparse? #{0A0A0B0A} [<any> thru #{0A} 2 <any>])
    (not uparse? #{0A} [to [#{0A}]])
    (not uparse? #{0A} [to [#{0A}] <end>])
    (uparse? #{0A} [to [#{0A}] <any>])
    (uparse? #{0A} [thru [#{0A}]])
    (uparse? #{0A} [thru [#{0A}] <end>])
    (not uparse? #{0A} [thru [#{0A}] <any>])
    (uparse? #{0A0B} [to [#{0A}] 2 <any>])
    (uparse? #{0A0B} [thru [#{0A}] <any>])
    (uparse? #{0A0A0A0B} [to [#{0A}] to <end>])
    (uparse? #{0A0A0B0A} [<any> thru [#{0A}] 2 <any>])
    (uparse? #{99990A0B0C} [to [#"^L" | #{0B} | #{0A}] 3 <any>])
    (uparse? #{99990A0B0C} [to [#{0A} | #{0B} | #"^L"] 3 <any>])
    (uparse? #{99990A0B0C} [thru [#"^L" | #{0B} | #{0A}] 2 <any>])
    (uparse? #{99990A0B0C} [thru [#{0A} | #{0B} | #"^L"] 2 <any>])
    (uparse? #{0B0B0A0A0A0C} [thru 3 #{0A} #"^L"])
    (uparse? #{0B0B0A0A0A0C} [thru 3 #{0A} #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru 3 wa #"^L"])
    (uparse? #{0B0B0A0A0A0C} [thru [3 #{0A}] #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru some #{0A} #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru [some #{0A}] #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru [some #x | #{0A0A0A}] #{0C}])
    (uparse? bin [thru #{DEADBEEF} <any>])
    (
        res: 0
        did all [
            uparse? bin [thru #{CAFE} <any> res: across to # to <end>]
            res = #{BABE}
        ]
    )
    (
        res: 0
        did all [
            uparse? bin [thru #{BABE} res: to <end>]
            9 = index? res
        ]
    )
    (not uparse? #{} [to #{0A}])
    (not uparse? #{} [to #"^/"])
    (not uparse? #{} [to [#{0A}]])
    (not uparse? #{} [to [#"^/"]])
]

[
    "binary-bitsets"

    (
        bs: charset [16 - 31 #"^/" - #"^O"]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (uparse? #{0A0B0C} [some bs])
    (not uparse? #{010203} [some bs])
    (uparse? #{0A0B0C} [some [bs]])
    (not uparse? #{010203} [some [bs]])
    (uparse? #{0A0B0C} [some wbs])
    (not uparse? #{010203} [some wbs])
    (uparse? #{0A0B0C} [some wbs2])
    (not uparse? #{010203} [some wbs2])
    (uparse? #{0A0B0C} [bs bs bs])
    (not uparse? #{010203} [bs bs bs])
    (uparse? #{0A0B0C} [[bs] [bs] [bs]])
    (not uparse? #{010203} [[bs] [bs] [bs]])
    (uparse? #{0A0B0C} [wbs wbs wbs])
    (not uparse? #{010203} [wbs wbs wbs])
    (uparse? #{0A0B0C} [wbs2 wbs2 wbs2])
    (not uparse? #{010203} [wbs2 wbs2 wbs2])
]

[
    (
        bs: charset [not 1 - 3 #"^/" - #"^O"]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (not uparse? #{0A0B0C} [some bs])
    (not uparse? #{010203} [some bs])
    (uparse? #{070809} [some bs])
    (not uparse? #{0A0B0C} [bs bs bs])
    (not uparse? #{010203} [bs bs bs])
    (uparse? #{070809} [bs bs bs])
    (
        digit: charset [0 - 9]
        did all [
            uparse? #{0BADCAFE010203} [to digit p: <here> 3 <any>]
            p = #{010203}
        ]
    )
]

[
    "binary-modify"

    (
        ws: charset " ^- ^/^M"
        not-ws: complement ws
        true
    )
    (error? try [uparse? #{} [remove]])
    (not uparse? #{} [remove <any>])
    (
        bin: #{0A}
        did all [
            uparse? bin [remove <any>]
            bin = #{}
        ]
    )
    (
        bin: #{0A0B0A}
        did all [
            uparse? bin [some [#{0A} | remove #{0B}]]
            bin = #{0A0A}
        ]
    )
    (
        bin: #{DEAD00BEEF}
        did all [
            uparse? bin [remove thru ws #{BEEF}]
            bin = #{BEEF}
        ]
    )
    (
        bin: #{DEAD00BEEF}
        did all [
            uparse? bin [remove #{DEAD} <any> #{BEEF}]
            bin = #{00BEEF}
        ]
    )
    (did all [
        uparse? s: #{00DE00AD00} [while [remove ws | <any>]]
        s = #{DEAD}
    ])
    (did all [
        uparse? s: #{00DE00AD00} [while [remove ws | <any>]]
        s = #{DEAD}
    ])
    (
        bin: #{DEAD0001020300BEEF}
        digit: charset [1 - 9]
        did all [
            uparse? bin [while [remove [some digit #] | <any>]]
            bin = #{DEAD00BEEF}
        ]
    )
    (did all [
        uparse? bin: #{} [insert (#"^A")]
        bin = #{01}
    ])
    (did all [
        uparse? bin: #{0A0A} [<any> insert (#{0B}) <any>]
        bin = #{0A0B0A}
    ])
    (did all [
        uparse? bin: #{} [p: <here> insert (#{0A}) seek (p) remove #{0A}]
        bin = #{}
    ])
    (did all [
        uparse? bin: #{DEADBEEF} [
            some [<any> p: <here> insert (#)] seek (p) remove <any>
        ]
        bin = #{DE00AD00BE00EF}
    ])
    (did all [
        uparse? bin: #{01} [change <any> (#{0A})]
        bin = #{0A}
    ])
    (did all [
        uparse? bin: #{010203} [change [3 <any>] (#{0A})]
        bin = #{0A}
    ])
    (
        digit: charset [1 - 9]
        did all [
            uparse? bin: #{010A020B03} [some [change digit (#{00}) | <any>]]
            bin = #{000A000B00}
        ]
    )
    (did all [
        uparse? bin: #{010203} [change 3 <any> (99)]
        bin = #{63}
    ])
    (did all [
        uparse? bin: #{BEADBEEF} [some [change #{BE} #{DE} | <any>]]
        bin = #{DEADDEEF}
    ])
    (did all [
        uparse? bin: #{0A0B0C03040D0E} [
            some [to digit change [some digit] (#{BEEF})] 2 <any>
        ]
        bin = #{0A0B0CBEEF0D0E}
    ])
]

[
    "binary-misc"

    (
        wa: [#{0A}]
        wb: [#{0B}]
        true
    )
    (uparse? #{} [break])
    (not uparse? #{0A} [break])
    (uparse? #{0A} [[break #{0B}] #{0A}])
    (uparse? #{0A} [[#{0B} | break] #{0A}])
    (uparse? #{0A0A} [some [#{0B} | break] 2 #{0A}])
    (uparse? #{0A0A} [some [#{0B} | [break]] 2 #{0A}])
    (not uparse? #{0A0A} [some [#{0B} | 2 [#"^L" | break]] 2 #{0A}])
    (not uparse? #{} [false])
    (not uparse? #{0A} [#{0A} false])
    (not uparse? #{0A} [[false]])
    (not uparse? #{0A} [false | false])
    (not uparse? #{0A} [[false | false]])
    (not uparse? #{0A} [#{0B} | false])
    (not uparse? #{} [not <end>])
    (uparse? #{0A} [not #{0B} #{0A}])
    (not uparse? #{0A} [not <any>])
    (not uparse? #{0A} [not <any> <any>])
    (uparse? #{0A} [not [#{0B}] #{0A}])
    (uparse? #{0A} [not wb #{0A}])
    (not uparse? #{0A0A} [not [#{0A} #{0A}] to <end>])
    (uparse? #{0A0A} [not [some #{0B}] to <end>])
    (uparse? #{0A0A} [opt some further [#"^L" | not #{0B}] 2 <any>])
    (not uparse? #{} [reject])
    (not uparse? #{0A} [reject #{0A}])
    (not uparse? #{0A} [reject wa])
    (not uparse? #{0A} [[reject] #{0A}])
    (uparse? #{0A} [[reject #{0B}] | #{0A}])
    (not uparse? #{0A} [[#{0B} | reject] #{0A}])
    (uparse? #{0A} [[#{0B} | reject] | #{0A}])
    (uparse? #{0A0A} [some reject | 2 #{0A}])
    (uparse? #{0A0A} [some [reject] | 2 #{0A}])
    (uparse? #{} [blank])
    (uparse? #{0A} [<any> blank])
    (uparse? #{0A} [blank <any> blank])
    (uparse? #{0A} [#{0A} blank])
    (uparse? #{0A} [blank #{0A} blank])
    (uparse? #{0A} [wa blank])
    (uparse? #{0A} [blank wa blank])
    (uparse? #{0A} [[#{0B} | blank] #{0A}])
    (uparse? #{0A} [[#{0B} | [blank]] #{0A}])
    (uparse? #{0A} [[[#{0B} | [blank]]] #{0A}])
    (uparse? #{} [opt blank])
    (uparse? #{} [opt #{0A}])
    (uparse? #{0A} [opt #{0A}])
    (uparse? #{0A} [opt #{0B} #{0A}])
    (uparse? #{0A} [opt [#{0A}]])
    (uparse? #{0A} [opt wa])
    (uparse? #{0A} [opt <any>])
    (uparse? #{0A0B0C} [<any> opt #{0B} <any>])
]

[
    (
        x: blank
        true
    )
    (uparse? #{020406} [while [x: across <any> :(even? first x)]])
    (not uparse? #{01} [x: across <any> :(even? first x)])
    (not uparse? #{0105} [some [x: across <any> :(even? first x)]])
    (uparse? #{} [while #{0A}])
    (uparse? #{} [while #{0B}])
    (uparse? #{0A} [while #{0A}])
    (not uparse? #{0A} [while #{0B}])
    (uparse? #{0A} [while #{0B} <any>])
    (uparse? #{0A0B0A0B} [while [#{0B} | #{0A}]])
    (error? try [uparse? #{} [ahead]])
    (uparse? #{0A} [ahead #{0A} #{0A}])
    (uparse? #{01} [ahead [#{0A} | #"^A"] <any>])
]

[
    "binary-part"

    (
        input: #{DEADBEEF}
        input2: #{0A0A0A0B0B}
        letters: charset [#­ - #Þ]
        true
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any>] 3
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] 4
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any> <any>] 4
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            uparse?/part next input [v: across 3 <any>] 3
            v = #{ADBEEF}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across to #o <any>] 3
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across to #{EF} <any>] 5
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 letters] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 letters] 3
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input2 [v: across 3 #{0A}] 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input2 [v: across 3 #{0A}] 3
            v = #{0A0A0A}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] skip input 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any>] skip input 3
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 <any>] skip input 4
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 <any> <any>] skip input 4
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            uparse?/part next input [v: across 3 <any>] skip input 4
            v = #{ADBEEF}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across to #o <any>] skip input 3
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across to #{EF} <any>] skip input 5
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input [v: across 3 letters] skip input 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input [v: across 3 letters] skip input 3
            v = #{DEADBE}
        ]
    )
    (
        v: blank
        did all [
            not uparse?/part input2 [v: across 3 #{0A}] skip input2 2
            blank? v
        ]
    )
    (
        v: blank
        did all [
            uparse?/part input2 [v: across 3 #{0A}] skip input2 3
            v = #{0A0A0A}
        ]
    )
]

[
    https://github.com/red/red/issues/2515

    (uparse? "this one is" ["this" to "is" "is"])
]

[
    https://github.com/red/red/issues/2561

    ([] = uparse? "" [collect [keep to <end>]])
    ([] = uparse? "" [collect [keep across to <end>]])
]

[
    https://github.com/red/red/issues/2818

    (uparse? "abc" [to [s: <here> "bc"] 2 <any>])
    (uparse? "abc" [to [s: <here> () "bc"] 2 <any>])
    (uparse? "abc" [to [s: <here> (123) "bc"] 2 <any>])
]

[
    https://github.com/red/red/issues/3108

    (uparse? [1] [some further [to <end>]])
    (uparse? [1] [some further [to [<end>]]])
    (
        partition3108: function [elems [block!] size [integer!]] [
            uparse elems [
                collect some further [
                    keep ^ across repeat (size) <any>
                    | keep ^ collect keep across to <end>
                ]          ; |-- this --| single collect keep is superflous
            ]
        ]
        [[1 2] [3 4] [5 6] [7 8] [9]] = partition3108 [1 2 3 4 5 6 7 8 9] 2
    )
]

[
    https://github.com/red/red/issues/3927

    (not uparse? "bx" [some further [not "b" | <any>]])
]

[
    https://github.com/red/red/issues/3357

    (
        uparse? x3357: [] [insert ^('foo)]
        x3357 = [foo]
    )(
        uparse? x3357b: [] [insert ^(the foo)]
        x3357b = [foo]
    )
]

[
    https://github.com/red/red/issues/3427

    (uparse?/part %234 ["23" thru [<end>]] 3)
    (uparse?/part %234 ["23" to [<end>]] 3)
    (uparse?/part %234 ["23" to <end>] 3)
    (
        count-up i 4 [
            assert [uparse?/part "12" ["1" to [<end>]] i]
        ]
        true
    )
]

[
    https://github.com/red/red/issues/4101

    (uparse? [a/b] ['a/b])
    (error? try [uparse? [a/b] [a/b]])
    (error? try [uparse? [a b c] [change 3 word! d/e]])
    (error? try [uparse? [a/b c d] [remove a/b]])
    (error? try [uparse? [c d] [insert a/b 2 word!]])
]

[
    https://github.com/red/red/issues/4318

    (
        x4318: 0
        did all [
            error? try [uparse? [] [x4318: across]]
            error? try [uparse? [] [x4318:]]
            zero? x4318
        ]
    )
]

[
    https://github.com/red/red/issues/4198

    ; !!! It's not clear whether KEEP PICK is supposed to splice or not...
    ; apparently not if a block comes from a group (?!)  UPARSE is much more
    ; consistent in its rules.

    ([[a b]] = uparse? [] [collect keep ^([a b])])
    ([a] = uparse? [] [collect keep ^('a)])
]

[
    https://github.com/red/red/issues/4591

    (uparse? [] [0 [ignore me]])
    (uparse? [] [0 "ignore me"])
    (uparse? [] [0 0 [ignore me]])
    (uparse? [] [0 0 "ignore me"])
    (not uparse? [x] [0 0 'x])
    (not uparse? " " [0 0 space])
]

[
    https://github.com/red/red/issues/4678

    (not uparse? to binary! "#(" [blank!])
    (not uparse? to binary! "(" [blank!])
    (not uparse? to binary! "[" [blank!])
]

[
    https://github.com/red/red/issues/4682

    (uparse? to binary! {https://example.org"} [
        match: across url! (assert [https://example.org == to url! match])
    ])
    (uparse? to binary! {a@b.com"} [
        match: across email! (assert [a@b.com == to email! to text! match])
    ])
]

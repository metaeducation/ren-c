; %bridge-notation.parse.test.reb
;
; https://www.tistis.nl/pbn/

[
(
whitespace: [some [space | newline]]

suit-order: [♣ ♦ ♥ ♠]
direction-order: [N E S W]
rank-order: [2 3 4 5 6 7 8 9 T J Q K A]

one-hand-rule: [
    let suit: ('♣)
    collect [repeat 4 [
        opt some [
            let rank: any @rank-order
            keep (join word! [suit, rank = 'T then [10] else [rank]])
        ]
        inline (
            suit: select suit-order suit
            if suit ["."] else $[ahead [whitespace | <end>]]
        )
    ]]
    |
    (panic "Invalid Hand information in PBN")
]

pbn-to-hands: func [
    "Convert portable bridge notation to BLOCK!-structured hands"

    return: [object!]
    pbn [text!]
    <local> start direction
][
    let hands: parse pbn [gather [
        opt whitespace  ; We allow leading whitespace, good idea?

        [
            start: any @direction-order
            | (panic "PBN must start with N, E, S, or W")
        ]
        direction: (start)

        [":" | (panic "PBN second character must be `:`")]

        [repeat 4 [
            emit (direction): one-hand-rule (  ; e.g. [emit N: ...]
                direction: (select direction-order direction) else [
                    first direction-order
                ]
            )
            opt whitespace  ; Is more than one space between hands ok?
        ]
        |
        (panic "PBN must have 4 hand definitions")]
    ]]

    assert [direction = start]  ; skipping around should have cycled
    return hands
],
okay)

(
    hands: pbn-to-hands --[
        N:QJ6.K652.J85.T98 873.J97.AT764.Q4 K5.T83.KQ9.A7652 AT942.AQ4.32.KJ3
    ]--
    assert [
        hands.N = [♣Q ♣J ♣6 ♦K ♦6 ♦5 ♦2 ♥J ♥8 ♥5 ♠10 ♠9 ♠8]
        hands.E = [♣8 ♣7 ♣3 ♦J ♦9 ♦7 ♥A ♥10 ♥7 ♥6 ♥4 ♠Q ♠4]
        hands.S = [♣K ♣5 ♦10 ♦8 ♦3 ♥K ♥Q ♥9 ♠A ♠7 ♠6 ♠5 ♠2]
        hands.W = [♣A ♣10 ♣9 ♣4 ♣2 ♦A ♦Q ♦4 ♥3 ♥2 ♠K ♠J ♠3]
    ]
)]

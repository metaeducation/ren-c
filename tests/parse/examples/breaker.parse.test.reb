; breaker.parse.test.reb

[
    (breaker: func [return: [block!] text [text!]] [
        let capturing
        let inner
        return parse text [collect until <end> [
            (capturing: null)
            opt keep between <here> ["$(" (capturing: okay) | <end>]
            inline (? if capturing $[
                inner: between <here> ")"
                keep (as word! inner)
            ])
        ]]
    ], ok)

    (["abc" def "ghi"] = breaker "abc$(def)ghi")
    ([] = breaker "")
    (["" abc "" def "" ghi] = breaker "$(abc)$(def)$(ghi)")
]

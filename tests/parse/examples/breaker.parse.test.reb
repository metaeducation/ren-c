; breaker.parse.test.reb

[
    (did breaker: func [return: [block!] text [text!]] [
        let capturing
        let inner
        return parse text [collect try some [
            not <end>
            (capturing: false)
            keep try between <here> ["$(" (capturing: true) | <end>]
            :(if capturing '[
                inner: between <here> ")"
                keep (as word! inner)
            ])
        ]]
    ])

    (["abc" def "ghi"] = breaker "abc$(def)ghi")
    ([] = breaker "")
    (["" abc "" def "" ghi] = breaker "$(abc)$(def)$(ghi)")
]

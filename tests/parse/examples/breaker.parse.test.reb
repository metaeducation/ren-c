; breaker.parse.test.reb

[
    (did /breaker: func [return: [block!] text [text!]] [
        let capturing
        let inner
        return parse text [collect while [not <end>] [
            (capturing: 'no)
            opt keep between <here> ["$(" (capturing: 'yes) | <end>]
            :(if yes? capturing '[
                inner: between <here> ")"
                keep (as word! inner)
            ])
        ]]
    ])

    (["abc" def "ghi"] = breaker "abc$(def)ghi")
    ([] = breaker "")
    (["" abc "" def "" ghi] = breaker "$(abc)$(def)$(ghi)")
]

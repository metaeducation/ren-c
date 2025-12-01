; %split.parse.test.r

[
    (
        split-test5: func [
            series [text!]
            dlm [text! char?]
            {value rule}
        ][
            rule: complement charset dlm
            return parse series [collect opt some [
                keep value: across some rule | next
            ]]
        ]
        ok
    )
    (["Hello" "bright" "world!"] = split-test5 "Hello bright world!" space)
    (["Hell" "bright" "w" "rld!"] = split-test5 "Hello bright world!" " o")
]

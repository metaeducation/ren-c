; %html-parser.parse.test.reb
;
; Recursive parser (uses `rule` inside `rule`).  Requires STOP to work.

[
    (
        html: {
^-^-^-<html>
^-^-^-^-<head><title>Test</title></head>
^-^-^-^-<body><div><u>Hello</u> <b>World</b></div></body>
^-^-^-</html>
^-^-}
        true
    )

    ; This version of the parser is roughly equivalent to what was in the
    ; %parse-test.red file.  Differences are that you have to KEEP the rule
    ; instead of it being collected implicitly, and STOP is used instead of
    ; BREAK in order to keep going with the rule.
    (
        ws: charset " ^-^/^M"
        res: uparse html rule: [
            collect maybe some [
                ws
                |
                "</" thru ">" stop
                |
                "<" name: across to ">" <any>
                keep ^(load-value name) opt keep ^rule
                |
                str: across to "<" keep (str)
            ]
        ]
        res = [
            html [head [title ["Test"]] body [div [u ["Hello"] b ["World"]]]]
        ]
    )

    (
        ws: charset " ^-^/^M"
        res: uparse html rule: [
            collect maybe some [
                ws
                |
                "</" thru ">" stop
                |
                keep ^ to-word/ [between "<" ">"], opt keep ^rule
                |
                keep across to "<"  ; may be end tag or new start tag
            ]
        ]
        res = [
            html [head [title ["Test"]] body [div [u ["Hello"] b ["World"]]]]
        ]
    )
]

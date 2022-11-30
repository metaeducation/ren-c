; %argtest.parse.reb
;
; This is a little test made to try adjusting rebol-server's webserver.reb to
; use a PARSE rule instead of an "ITERATE".  Since it was written, keep it.

[
    (did argtest: func [return: [block!] args [block!]] [
        let port: null
        let root-dir: null
        let access-dir: null
        let verbose: null
        let dir
        parse args [opt some [
            "-a", access-dir: [
                <end> (true)
                | "true" (true)
                | "false" (false)
                | dir: text!, (to-file dir)  ; manual form, use TO-FILE/ below
            ]
            |
            ["-h" | "-help" | "--help" || (-help, quit)]
            |
            verbose: [
                "-q" (0)
                | "-v" (2)
            ]
            |
            port: subparse text! integer!
            |
            root-dir: to-file/ text!
        ]]
        else [
            return null
        ]
        return reduce/predicate [access-dir port root-dir verbose] :reify
    ])

    ([~null~ ~null~ ~null~ 2] = argtest ["-v"])
    ([~true~ ~null~ ~null~ ~null~] = argtest ["-a"])
    ([~true~ ~null~ ~null~ ~null~] = argtest ["-a" "true"])
    ([~false~ ~null~ ~null~ ~null~] = argtest ["-a" "false"])
    ([%something ~null~ ~null~ ~null~] = argtest ["-a" "something"])
    ([~null~ 8000 ~null~ ~null~] = argtest ["8000"])
    ([~null~ 8000 ~null~ 2] = argtest ["8000" "-v"])
    ([~null~ 8000 %foo/bar 2] = argtest ["8000" "-v" "foo/bar"])
]

; %argtest.parse.reb
;
; This is a little test made to try adjusting rebol-server's webserver.reb to
; use a PARSE rule instead of an "ITERATE".  Since it was written, keep it.

[
    (did argtest: func [args [block!]] [
        let port: _
        let root-dir: _
        let access-dir: _
        let verbose: _
        let dir
        let arg
        uparse args [while [
            "-a", access-dir: [
                <end> (true)
                | "true" (true)
                | "false" (false)
                | dir: <any>, (to-file dir)
            ]
            |
            ["-h" | "-help" | "--help" || (-help, quit)]
            |
            verbose: [
                "-q" (0)
                | "-v" (2)
            ]
            |
            port: into <any> integer!
            |
            arg: <any> (root-dir: to-file arg)
        ]]
        else [
            return null
        ]
        return reduce [access-dir port root-dir verbose]
    ])

    ([_ _ _ 2] = argtest ["-v"])
    ([#[true] _ _ _] = argtest ["-a"])
    ([#[true] _ _ _] = argtest ["-a" "true"])
    ([#[false] _ _ _] = argtest ["-a" "false"])
    ([%something _ _ _] = argtest ["-a" "something"])
    ([_ 8000 _ _] = argtest ["8000"])
    ([_ 8000 _ 2] = argtest ["8000" "-v"])
    ([_ 8000 %foo/bar 2] = argtest ["8000" "-v" "foo/bar"])
]

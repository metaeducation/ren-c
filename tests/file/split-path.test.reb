
(
    [file dir]: split-path %./
    [%./ ~null~] = reduce [dir, reify file]
)
(
    [file dir]: split-path %../
    [%../ ~null~] = reduce [dir, reify file]
)
(
    [file dir]: split-path %test
    [%./ %test] = reduce [dir file]
)
(
    [file dir]: split-path %test/
    [%./ %test/] = reduce [dir file]
)
(
    [file dir]: split-path %test/test/
    [%test/ %test/] = reduce [dir file]
)
(
    [file dir]: split-path http://rebol.com/index.html
    [http://rebol.com/ %index.html] = reduce [dir file]
)


; These tests are derived from a discussion on the Red GitHub issues:
;
; https://github.com/red/red/issues/5024
;
; Review what the best design is as time permits.  It is nice to know when
; the components are not there, but the loss of a datatype (e.g. _ vs. %"")
; makes it hard to know what the result should be when trying to join back.
(
    split-path-tests: compose/deep [
        %foo                            [_ %foo]
        %""                             [_ %""]
        %/c/rebol/tools/test/test.r     [%/c/rebol/tools/test/ %test.r]
        %/c/rebol/tools/test/           [%/c/rebol/tools/ %test/]
        %/c/rebol/tools/test            [%/c/rebol/tools/ %test]
        %/c/test/test2/file.x           [%/c/test/test2/ %file.x]
        %/c/test/test2/                 [%/c/test/ %test2/]
        %/c/test/test2                  [%/c/test/ %test2]
        %/c/test                        [%/c/ %test]
        %//test                         [%// %test]
        %/test                          [%/ %test]
        %/c/                            [%/ %c/]
        %/                              [%/ _]
        %//                             [%/ %/]
        %.                              [%./ _]
        %./                             [%./ _]
        %./.                            [%./ %./]
        %..                             [%../ _]
        %../                            [%../ _]
        %../..                          [%../ %../]
        %../../test                     [%../../ %test]
        %foo/..                         [%foo/ %../]
        %foo/.                          [%foo/ %./]
        %foo/../.                       [%foo/../ %./]
        %foo/../bar                     [%foo/../ %bar]
        %foo/./bar                      [%foo/./ %bar]
        %/c/foo/../bar                  [%/c/foo/../ %bar]
        %/c/foo/./bar                   [%/c/foo/./ %bar]
        http://www.rebol.com/index.html [http://www.rebol.com/ %index.html]
        http://www.rebol.com/           [http:// %www.rebol.com/]
        http://www.rebol.com            [http:// %www.rebol.com]
        http://                         [http:/ %/]  ; ???
        http://..                       [http:// %../]
        http://.                        [http:// %./]
        http://../.                     [http://../ %./]
        http://../bar                   [http://../ %bar]
        http://./bar                    [http://./ %bar]
        (at %/vol/dir/file.r 6)         [%dir/ %file.r]
    ]
    for-each [test result] split-path-tests [
        [file dir]: split-path test
        if result <> actual: reduce [any [dir _], any [file _]] [
            fail [mold test 'expected mold result "but got" mold actual]
        ]
        res: if file and dir [
            join dir file
        ] else [
            any [file, dir]
        ]
        if test <> res [
            fail ["REJOIN quality failed:" mold test mold res]
        ]
    ]
    true
)

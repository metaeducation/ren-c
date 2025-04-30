; functions/math/not.r

(null = not okay)
(okay = not null)

(null = not :append)
(null = not :abs)
(null = not func [] [])

(null = not type of +/)
(null = not integer!)

(
    for-each 'item compose [
        a
        #{}
        (charset "")
        []
        #"a"
        datatype!
        1/1/2007
        0.0
        me@mydomain.com
        %myfile
        :a
        0
        #1444
        'a/b
        'a
        (to map! [])
        $0.00
        _
        (make object! [])
        (0x0)
        ()
        a/b
        (make port! http://)
        /refinement
        a.b:
        a:
        ""
        <tag>
        1:00
        1.2.3
        http://
    ][
        if null = not item [
            continue
        ]

        fail ["NOT did not return ~null~ antiform for:" mold item]
    ]
    ok
)

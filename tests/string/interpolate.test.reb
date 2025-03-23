; %interpolate.test.reb

; Smoke tests
[
    ("1" = interpolate "(1)")
    ("1 2" = interpolate "(1) (2)")

    ("1" = compose2 @{{}} "{{1}}")
    ("1 2" = compose2 @{{}} "{{1}} {{2}}")

    ("1 {2}" = compose2 @{{}} "{{1}} {2}")

    ("[( 1 )]" = compose2 @[({})] "[( [({1})] )]")
    ("[(1)]" = compose2 @[({})] "[([({1})])]")

    ~scan-missing~ !! (compose2 @[({})] "[({")
    ("[(" = compose2 @[({})] "[(")
]

; Have to back out and start over if pattern not fully matched
[
    (compose2 @(([])) --{((backing (([reverse "tuo"]))}--)
]

(
    let foo: 1000
    let text: interpolate "Hello (foo + 20) World"
    text = "Hello 1020 World"
)

(
    let foo: 1000
    let tag: interpolate <Hello (foo + 20) Tag (foo - 696) World!>
    tag = <Hello 1020 Tag 304 World!>
)

(
    let id: 2114
    let url: interpolate https://(reverse of 'info.rebol.forum)/t/(id)
    url = https://forum.rebol.info/t/2114
)

; Raised errors bubble out
[
    ~nothing-to-take~ !! (
        let block: [a]
        assert ["Item is a!" = interpolate "Item is (take block)!"]
        interpolate "Item is (take block)!"
    )

    (
        let block: [a]
        all [
            "Item is a!" = interpolate "Item is (take block)!"
            not try interpolate "Item is (take block)!"
        ]
    )
]

; Only "simple" patterns (nested lists) for now
[
    ~???~ !! (compose2 @(<*>) "(this is) (<*> not working yet)")
    ~???~ !! (compose2 @(() ()) "(shouldn't work either)")
]

; better-than-nothing ADAPT tests

(
    x: 10
    foo: adapt 'any [x: 20]
    foo [1 2 3]
    x = 20
)
(
    capture: blank
    foo: adapt 'any [capture: block]
    did all [
      foo [1 2 3]
      capture = [1 2 3]
    ]
)
(
    v: copy []
    append-v: specialize 'append [
        series: v
    ]
    adapted-append-v: adapt 'append-v [
        value: to integer! value
        only: true
    ]
    adapted-append-v "10"
    adapted-append-v "20"
    v = [10 20]
)

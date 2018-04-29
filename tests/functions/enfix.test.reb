; %enfix.test.reb

(action! = type-of :+)
(true = enfixed? '+)

(
    foo: :+
    did all [
        not enfixed? 'foo
        error? trap [1 foo 2]
        3 = foo 1 2
    ]
)
(
    set/enfix 'foo :+
    did all [
        enfixed? 'foo
        3 = 1 foo 2
    ]
)
(
    set/enfix 'postfix-thing func [x] [x * 2]
    all [
       enfixed? 'postfix-thing
       20 = (10 postfix-thing)
    ]
)

(3 == do reduce [get '+ 1 2])

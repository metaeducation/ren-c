; %alias.test.r
;
; ALIAS is a reimagination of a historical Rebol ability to make a
; variable refer to another.  But instead of symbolic equivalence it
; is like a getter/setter, using "dual" representation.

(all {
    x: 10
    y: z: alias $x
    x = 10, y = 10, z = 10
    y: 20
    x = 20, y = 20, z = 20
    z: 30
    x = 30, y = 30, z = 30
}) 

(all {
    x: 10
    y: decay z: alias $x
    x = 10, y = 10, z = 10
    y: 20
    x = 10, y = 20, z = 10
    z: 30
    x = 30, y = 20, z = 30
})

(all {
    o: make object! [x: 10]
    y: z: alias $o.x
    o.x = 10, y = 10, z = 10
    y: 20
    o.x = 20, y = 20, z = 20
    z: 30
    o.x = 30, y = 30, z = 30
})

(all {
    o: make object! [x: 10]
    y: decay z: alias $o.x
    o.x = 10, y = 10, z = 10
    y: 20
    o.x = 10, y = 20, z = 10
    z: 30
    o.x = 30, y = 20, z = 30
})


; The ^PATH! type is new and needs testing.

(path! = type of '^a/b/c)
('^a = first '^a/b/c)
(3 = length of '^a/b/c)
("^^a/b/c" = (mold join path! @[^a b c]))

(obj: make object! [x: meta 10], 10 = obj.^x)
(obj: make object! [x: meta null], null = obj.^x)

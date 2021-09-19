; The META-PATH! type is new and needs testing.

(meta-path! = type of '^a/b/c)
('a = first '^a/b/c)
(3 = length of '^a/b/c)
("^^a/b/c" = (mold to meta-path! [a b c]))

(obj: make object! [x: 10], (the '10) = ^obj.x)
(obj: make object! [x: null], null = ^obj.x)

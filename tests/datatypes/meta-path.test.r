; The ^PATH! type is new and needs testing.

(metaform! = type of '^a/b/c)
('a = first '^a/b/c)
(3 = length of '^a/b/c)
("^^a/b/c" = (mold meta join path! @[a b c]))

(obj: make object! [x: lift 10], 10 = ^obj.x)
(obj: make object! [x: lift null], null = ^obj.x)

; The SYM-PATH! type is new and needs testing.

(sym-path! = type of @a/b/c)
('a = first @a/b/c)
(3 = length of @a/b/c)
("@a/b/c" = (mold to sym-path! [a b c]))

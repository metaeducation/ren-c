; The META-WORD! type is new and needs testing.

(any-word? '^foo)
("foo" = as text! '^foo)
(meta-word! = type of '^foo)

(x: 10, (the '10) = meta x)
(x: meta null, null = meta x)

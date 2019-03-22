; The SYM-WORD! type is new and needs testing.

(any-word? @foo)
("foo" = as text! @foo)
(sym-word! = type of @foo)

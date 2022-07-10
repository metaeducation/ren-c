; %parse-tag-index.test.reb
;
; Simple combinator, not much to go wrong.  Always succeeds.

(1 = parse* "aa" [<index>])
(2 = parse "aa" ["a" <index> elide thru <end>])
(3 = parse* "aa" ["aa" <index>])

(1 = parse [a a] [<index> elide to <end>])
(2 = parse* [a a] ['a <index>])
(3 = parse [a a] ['a 'a <index>])

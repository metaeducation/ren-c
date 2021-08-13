; %parse-tag-index.test.reb
;
; Simple combinator, not much to go wrong.  Always succeeds.

(1 = uparse* "aa" [<index>])
(2 = uparse "aa" ["a" <index> elide thru <end>])
(3 = uparse* "aa" ["aa" <index>])

(1 = uparse [a a] [<index> elide to <end>])
(2 = uparse* [a a] ['a <index>])
(3 = uparse [a a] ['a 'a <index>])

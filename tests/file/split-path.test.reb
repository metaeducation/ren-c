
(
    equal? [%./ _] split-path %./
)
(
    equal? [%../ _] split-path %../
)
(
    equal? [%./ %test] split-path %test
)
(
    equal? [%./ %test/] split-path %test/
)
(
    equal? [%test/ %test/] split-path %test/test/
)
(
    equal? [http://rebol.com/ %index.html] split-path http://rebol.com/index.html
)

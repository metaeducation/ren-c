; %n-queens.test.reb
;
; REN-C solution to the N Queens problem
; by iArnold
;
; Place all N queens on the NxN chessboard where every queen is to be placed
; on a field that is not taken nor covered by any of the other queens.
;
; We know the solutions are not symmetrical themselves but the solutions can
; be mirror images of other solutions or turned solutions.
;
; This solution works as follows:
;
; By symmetry we only need to test the first half of the row or column with
; the first queen. On an odd sized board we only cannot double the number of
; results for the middle position.
;
; We do not know or even care if we work horizontally or vertically and
; even if we are ; starting at the first or last row or column is irrelevant.
;
; We start with adding our first queen to the board. This is a number in a
; solution block. The we go to the next level and determine the free choices
; for our next queen. This is a collection of all fields minus the ones other
; queens cover. This propagates through all rows / columns. At the end we
; reach an empty set so no solution or a set of solutions.

[(
    number-of-solutions: 0

    reset-number-of-solutions: does [
        number-of-solutions: 0
    ]

    add-one-to-number-of-solutions: does [
        number-of-solutions: number-of-solutions + 1
    ]

    get-number-of-solutions: does [
        number-of-solutions
    ]

    ; This variable is to signal that results should not be duplicated for
    ; this value.
    ;
    odd-symmetry-limit: 0

    reset-symmetry-limit: does [
        odd-symmetry-limit: 0
    ]

    add-to-symmetry-limit: does [
        odd-symmetry-limit: odd-symmetry-limit + 1
    ]

    logic-countonly: 0
    solved-boards: null

    set-countonly-true: does [
        logic-countonly: 1
    ]

    set-countonly-false: does [
        logic-countonly: 0
    ]

    ; Function to print the board for a solution

    form-board: func [
        return: [text!]
        n [integer!]
        board-values [block!]
    ][
        let a: copy ""
        for-each 'b board-values [
            count-up 't b - 1 [append a ". "]
            append a "Q "
            count-up 't n - b [append a ". "]
            take/last a  ; don't want trailing space
            append a newline
        ]
        take/last a  ; don't want trailing newline
        return a
    ]

    ; Function for recursion
    add-queen: func [n [integer!]
        solution [block!]
        free-places [block!]
    ][
        ; Determine the free choices for this queen
        let forbidden-places: copy []
        let can-see: 1
        let rsolution: reverse copy solution
        for-each 'sol rsolution [
            append forbidden-places sol
            append forbidden-places sol + can-see
            append forbidden-places sol - can-see  ; too lazy for bounds check
            can-see: can-see + 1
        ]
        let free-choices: exclude free-places forbidden-places
        if not empty? free-choices [
            either n = 1 [
                ; now check for a solution, no more recursion possible
                for-each 'place free-choices [
                    append solution place
                    if logic-countonly = 0 [
                        append solved-boards form-board // [
                            length-of solution
                            solution
                        ]
                    ]
                    add-one-to-number-of-solutions
                    if any [
                        odd-symmetry-limit > first solution
                        odd-symmetry-limit = 0
                    ][
                        if logic-countonly = 0 [
                            append solved-boards form-board // [
                                length-of solution
                                reverse copy solution
                            ]
                        ]
                        add-one-to-number-of-solutions
                    ]
                    clear skip solution ((length-of solution) - 1)
                ]
            ][
                for-each 'place free-choices [
                    append solution place
                    add-queen n - 1 solution free-places
                    clear skip solution ((length-of solution) - 1)
                ]
            ]
        ]
    ]

    ; Function for main solution

    solve-n-queens: func [
        return: "The number of solutions, and solutions (if requested)"
            [integer! ~[integer! block!]~]
        n "The number queens on to place the board of size nxn"
            [integer!]
        /countonly "Only print the number of solutions found"
    ][
        ; We need to know this within our recursive function
        either countonly [
            set-countonly-true
            solved-boards: null
        ][
            solved-boards: copy []
            set-countonly-false
        ]

        ; make a basic block of the row / column numbers
        let places-block: copy []
        count-up 'i n [append places-block i]

        reset-number-of-solutions

        ; We need only do half of the first row/column for reasons of symmetry
        let half: either odd? n [(n + 1) / 2] [n / 2]
        reset-symmetry-limit
        if odd? n [
            repeat half [add-to-symmetry-limit]
        ]

        either n > 1 [
            count-up 'first-queen half [
                let solution: copy []
                append solution first-queen

                add-queen n - 1 solution places-block

                clear solution  ; okay because we are in the outermost loop
            ]
        ][
            add-one-to-number-of-solutions
            if logic-countonly = 0 [
                append solved-boards form-board n [1]
            ]
        ]

        if countonly [
            return get-number-of-solutions
        ]
        return pack [get-number-of-solutions solved-boards]
    ]
    ok
)


; Examples

(
    [num boards]: solve-n-queens 1
    all [
        num = 1
        boards = [{Q}]
    ]
)

(
    [num boards]: solve-n-queens 2
    all [
        num = 0
        boards = []
    ]
)

(
    [num boards]: solve-n-queens 3
    all [
        num = 0
        boards = []
    ]
)

(
    [num boards]: solve-n-queens 4
    all [
        num = 2
        boards = reduce // [/predicate trim/ [
            {. Q . .
             . . . Q
             Q . . .
             . . Q .}

            {. . Q .
             Q . . .
             . . . Q
             . Q . .}
        ]]
    ]
)

(
    [num boards]: solve-n-queens 5
    all [
        num = 10
        boards = reduce // [/predicate :trim, [
           {Q . . . .
            . . Q . .
            . . . . Q
            . Q . . .
            . . . Q .}

           {. . . Q .
            . Q . . .
            . . . . Q
            . . Q . .
            Q . . . .}

           {Q . . . .
            . . . Q .
            . Q . . .
            . . . . Q
            . . Q . .}

           {. . Q . .
            . . . . Q
            . Q . . .
            . . . Q .
            Q . . . .}

           {. Q . . .
            . . . Q .
            Q . . . .
            . . Q . .
            . . . . Q}

           {. . . . Q
            . . Q . .
            Q . . . .
            . . . Q .
            . Q . . .}

           {. Q . . .
            . . . . Q
            . . Q . .
            Q . . . .
            . . . Q .}

           {. . . Q .
            Q . . . .
            . . Q . .
            . . . . Q
            . Q . . .}

           {. . Q . .
            Q . . . .
            . . . Q .
            . Q . . .
            . . . . Q}

           {. . Q . .
            . . . . Q
            . Q . . .
            . . . Q .
            Q . . . .}
        ]]
    ]
)

    (92 = solve-n-queens/countonly 8)
]

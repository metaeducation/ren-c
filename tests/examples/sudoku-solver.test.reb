; %sudoku-solver.test.reb
;
; by iArnold
;
; based on the Python example from computerphile:
; https://www.youtube.com/watch?v=G_UYXzGuqvM&list=WL&index=228

[(
    input: [
        5 3 0 0 7 0 0 0 0
        6 0 0 1 9 5 0 0 0
        0 9 8 0 0 0 0 6 0
        8 0 0 0 6 0 0 0 3
        4 0 0 8 0 3 0 0 1
        7 0 0 0 2 0 0 0 6
        0 6 0 0 0 0 2 8 0
        0 0 0 4 1 9 0 0 5
        0 0 0 0 8 0 0 7 9
    ]
    output: null

    form-grid: func [grid [block!] <local> text] [
        text: copy ""
        count-up i 81 [
            append text grid.(i)
            either i mod 9 = 0 [
                append text newline
                if all [
                    i mod 27 = 0
                    i < 81
                ][
                    append text "------+-------+------^/"
                ]
            ][
                append text either any [
                    i mod 9 = 3
                    i mod 9 = 6
                ][
                    " | "
                ][
                    " "
                ]
            ]
        ]
        return text
    ]

    possible: func [
        y
        x
        n
        <local> x0 y0
    ][
        count-up i 9 [
            if n = input.(9 * (y - 1) + i) [
                return false
            ]
        ]
        count-up i 9 [
            if n = input.(9 * (i - 1) + x) [
                return false
            ]
        ]
        x0: ((to integer! (x - 1) / 3)) * 3 + 1
        y0: ((to integer! (y - 1) / 3)) * 3 + 1
        count-up i 3 [
            count-up j 3 [
                if n = input.(9 * (y0 + (i - 1) - 1) + (x0 + (j - 1))) [
                    return false
                ]
            ]
        ]
        return true
    ]

    solve: func [] [
        count-up y 9 [
            count-up x 9 [
                if 0 = input.(9 * (y - 1) + x) [
                    count-up n 9 [
                        if possible y x n [
                            input.(9 * (y - 1) + x): n
                            solve
                            input.(9 * (y - 1) + x): 0  ; backtracking
                        ]
                    ]
                    return none
                ]
            ]
        ]
        output: copy input  ; will backtrack and put the zeros back
    ]

    solve

    (form-grid output) = trim {
        5 3 4 | 6 7 8 | 9 1 2
        6 7 2 | 1 9 5 | 3 4 8
        1 9 8 | 3 4 2 | 5 6 7
        ------+-------+------
        8 5 9 | 7 6 1 | 4 2 3
        4 2 6 | 8 5 3 | 7 9 1
        7 1 3 | 9 2 4 | 8 5 6
        ------+-------+------
        9 6 1 | 5 3 7 | 2 8 4
        2 8 7 | 4 1 9 | 6 3 5
        3 4 5 | 2 8 6 | 1 7 9
    }
)]

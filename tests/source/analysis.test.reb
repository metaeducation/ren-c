; source/analysis.r

;
; Source analysis tests.  These check the source code for adherence to
; coding conventions (naming, indentation, column width, etc.)  These
; tests may evolve further into enforcing rules about the call graph
; and other statically-checkable aspects.
;
; At the moment, there are some failures of these tests.  They will be
; addressed in an ongoing fashion as the source is brought in line
; with the automated checking.
;

(
    rebsource: import %source-tools.reb
    source-analysis: rebsource.analyse/files
    save %../source-analysis.log source-analysis
    true
)
(not find source-analysis <whitespace-at-eol>)
(not find source-analysis <eof-eol-missing>)
(not find source-analysis <tabbed>)
(not find source-analysis <id-mismatch>)
(not find source-analysis <inconsistent-eol>)

; Currently failing. Uncomment, to work on cleaning this up.
;[not find source-analysis [<line-exceeds> 127]]

; Currently failing. Uncomment, to work on cleaning this up.
;[not find source-analysis <malloc>]

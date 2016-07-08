; functions/file/open.r
; bug#1422: "Rebol crashes when opening the 128th port"
[error? try [repeat n 200 [try [close open open join tcp://localhost: n]]] true]

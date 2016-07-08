; datatypes/file.r
[file? %myscript.r]
[not file? 1]
[file! = type-of %myscript.r]
; minimum
[file? %""]
[%"" == #[file! ""]]
[%"" == make file! 0]
[%"" == to file! ""]
["%%2520" = mold to file! "%20"]
; bug#1241
[file? %"/c/Program Files (x86)"]

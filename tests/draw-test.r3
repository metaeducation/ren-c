REBOL [
	Title: "Basic DRAW test"
	Date: 24-Sep-2010
	Author: "Cyphre"
	Purpose: "Tests many of the basic DRAW dialect commands, eg. lines, boxes, curves, fills etc."
]
do %gfx-pre.r3

black: 0.0.0
white: 255.255.255
red: 255.0.0
green: 0.255.0
blue: 0.0.255
yellow: 255.255.0

img1: load %dev-small.jpg

img1-alpha: copy img1
oft: 0x0
siz: img1-alpha/size - 1
repeat y siz/y [
	repeat x siz/x [
		pos: as-pair x + oft/x y + oft/y
		pix: pick img1-alpha pos
		pix/4: to-integer (x * (255 / siz/x))
		poke img1-alpha pos pix
	]
]

img2: make image! [44x44]
change/dup img2 255.0.0 22x22
change/dup at img2 22x0 0.255.0 22x22
change/dup at img2 0x22 0.0.255 22x22
change/dup at img2 22x22 255.255.0 22x22

draw-block: to-draw [

		pen 0.127.127
		fill-pen 218.188.189
		anti-alias on
		line-width 1
		line-cap square
		line-join bevel
				
		;ANTI-ALIAS 
		translate 25x25
		arc 0x0 25x25 0 90
		translate 50x0
		arc 0x0 25x25 0 180
		translate 0x50
		arc 0x0 25x25 0 270
		translate -50x0
		arc 0x0 25x25 0 360

		translate 105x-50
		arc 0x0 25x25 0 90 closed
		translate 50x0
		arc 0x0 25x25 0 180 closed
		translate 0x50
		arc 0x0 25x25 0 270 closed
		translate -50x0
		arc 0x0 25x25 0 360 closed

		
		;ARROW
		reset-matrix
		translate 200x0

		translate 10x0
		line 0x90 0x10
		arrow 1x0 
		translate 10x0
		line 0x90 0x10
		arrow 2x0 
		translate 10x0
		line 0x90 0x10
		arrow 0x1 
		translate 10x0
		line 0x90 0x10
		arrow 0x2 
		translate 10x0
		line 0x90 0x10
		arrow 1x2 
		translate 10x0
		line 0x90 0x10
		arrow 2x1 
		translate 10x0
		line 0x90 0x10
		arrow 1x1 
		translate 10x0
		line 0x90 0x10
		arrow 2x2 
		translate 10x0
		line 0x90 0x10
		arrow 0x0

		reset-matrix
		translate 300x0

		translate 0x10
		line 10x0 90x0
		arrow 1x0 
		translate 0x10
		line 10x0 90x0
		arrow 2x0 
		translate 0x10
		line 10x0 90x0
		arrow 0x1 
		translate 0x10
		line 10x0 90x0
		arrow 0x2 
		translate 0x10
		line 10x0 90x0
		arrow 1x2 
		translate 0x10
		line 10x0 90x0
		arrow 2x1 
		translate 0x10
		line 10x0 90x0
		arrow 1x1 
		translate 0x10
		line 10x0 90x0
		arrow 2x2 
		translate 0x10
		line 10x0 90x0
		arrow 0x0
		
		;BOX
		reset-matrix
		translate 400x0
		box 5x5 95x95
		translate 100x0
		box 5x5 95x45
		box 5x50 95x95 10
		translate 100x0
		box 5x5 45x95
		box 50x5 95x95 10
		translate 100x0
		box 5x5 95x95 10

		;CIRCLE
		reset-matrix
		translate 0x100
		circle 50x50 45
		translate 100x0
		circle 50x50 45 22.5
		translate 100x0
		circle 50x50 22.5 45

		;CURVE
		reset-matrix
		translate 300x100
		curve 0x0 100x0 100x100 
		translate 100x0
		curve 0x0 100x0 0x100 100x100
		
		;ELLIPSE
		reset-matrix
		translate 500x100
		ellipse 5x5 95x95
		translate 100x0
		ellipse 5x25 95x50
		translate 100x0
		ellipse 25x5 50x95

		
		;IMAGE
		reset-matrix
		translate 0x200
		image-filter bilinear
		image img1
		image-options border
		image 50x50 img1

		image-options no-border
		translate 100x0
		image img1 5x5 95x95
		translate 100x0
		image img1 30x5 70x95

		translate 100x0
		image 5x30 95x30 95x70 img1
		reset-matrix
		translate 450x250
		rotate 45
		image-options border
		image img1 -35x-35 35x35
		rotate -45
		translate 50x-50
		image-options no-border
		image img1 35x35 70x15 90x95 10x95
		translate 100x0
		image-options border
		image img1 15x5 95x20 55x75 20x93
		translate 100x0
		image-options border 255.0.0
		image img2 5x5
		image-options border 0.255.0
		image img2 49x5
		image-options border 0.0.255
		image img2 5x49
		image-options border 255.0.0
		image img2 49x49

		reset-matrix
		translate 0x300
		image-options no-border
		image-pattern repeat
		image img1 5x5 95x95
		translate 100x0
		image-pattern repeat 0x0 100x100
		image img1 5x5 95x95
		translate 100x0
		image-pattern repeat 25x25 100x100
		image img1 5x5 95x95
		translate 100x0
		image-pattern repeat 0x0 200x200
		image img1 5x5 95x95
		translate 100x0
		image-pattern reflect
		image img1 5x5 95x95
		translate 100x0
		image-pattern reflect 0x0 100x100
		image img1 5x5 95x95
		translate 100x0
		image-pattern reflect 25x25 100x100
		image img1 5x5 95x95
		translate 100x0
		image-pattern reflect 0x0 200x200
		image img1 5x5 95x95

		reset-matrix
		translate 0x400
		image-pattern repeat
		image img1 35x5 65x5 95x95 5x95
		translate 100x0
		image-pattern repeat 0x0 100x100
		image img1 35x5 65x5 95x95 5x95
		translate 100x0
		image-pattern repeat 25x25 100x100
		image img1 35x5 65x5 95x95 5x95
		translate 100x0
		image-pattern repeat 0x0 200x200
		image img1 35x5 65x5 95x95 5x95
		translate 100x0	
		image-pattern reflect
		image img1 35x5 65x5 95x95 5x95
		translate 100x0
		image-pattern reflect 0x0 100x100
		image img1 35x5 65x5 95x95 5x95
		translate 100x0
		image-pattern reflect 25x25 100x100
		image img1 35x5 65x5 95x95 5x95
		translate 100x0
		image-pattern reflect 0x0 200x200
		image img1 35x5 65x5 95x95 5x95

		;LINE
		reset-matrix
		translate 0x500

		line 5x5  95x5
		line 5x15 95x15
		line 5x25 95x25
		line 5x35 95x35
		line 5x45 95x45
		line 5x55 95x55
		line 5x65 95x65
		line 5x75 95x75
		line 5x85 95x85
		line 5x95 95x95
		
		translate 100x0
		line 5x5  5x95
		line 15x5 15x95
		line 25x5 25x95
		line 35x5 35x95
		line 45x5 45x95
		line 55x5 55x95
		line 65x5 65x95
		line 75x5 75x95
		line 85x5 85x95
		line 95x5 95x95

		translate 100x0
		line 5x5  95x5
		5x15 95x15
		5x25 95x25
		5x35 95x35
		5x45 95x45
		5x55 95x55
		5x65 95x65
		5x75 95x75
		5x85 95x85
		5x95 95x95

		translate 100x0
		line 5x5  5x95
		15x5 15x95
		25x5 25x95
		35x5 35x95
		45x5 45x95
		55x5 55x95
		65x5 65x95
		75x5 75x95
		85x5 85x95
		95x5 95x95
		
		translate 100x0
		line 5x85 15x95
		line 5x75 25x95
		line 5x65 35x95
		line 5x55 45x95
		line 5x45 55x95
		line 5x35 65x95
		line 5x25 75x95
		line 5x15 85x95
		line  5x5 95x95
		line 15x5 95x85
		line 25x5 95x75
		line 35x5 95x65
		line 45x5 95x55
		line 55x5 95x45
		line 65x5 95x35
		line 75x5 95x25
		line 85x5 95x15

		translate 100x0
		line 5x15 15x5
		line 5x25 25x5
		line 5x35 35x5
		line 5x45 45x5
		line 5x55 55x5
		line 5x65 65x5
		line 5x75 75x5
		line 5x85 85x5
		line 5x95 95x5
		line 15x95 95x15
		line 25x95 95x25
		line 35x95 95x35
		line 45x95 95x45
		line 55x95 95x55
		line 65x95 95x65
		line 75x95 95x75
		line 85x95 95x85

		translate 100x0
		line 5x85 15x95
		5x75 25x95
		5x65 35x95
		5x55 45x95
		5x45 55x95
		5x35 65x95
		5x25 75x95
		5x15 85x95
		 5x5 95x95
		15x5 95x85
		25x5 95x75
		35x5 95x65
		45x5 95x55
		55x5 95x45
		65x5 95x35
		75x5 95x25
		85x5 95x15

		translate 100x0
		line 5x15 15x5
		5x25 25x5
		5x35 35x5
		5x45 45x5
		5x55 55x5
		5x65 65x5
		5x75 75x5
		5x85 85x5
		5x95 95x5
		15x95 95x15
		25x95 95x25
		35x95 95x35
		45x95 95x45
		55x95 95x55
		65x95 95x65
		75x95 95x75
		85x95 95x85
		
		;POLYGON
		reset-matrix
		translate 0x600
		polygon 
		50x5
		62x40
		95x40
		68x60
		80x95
		50x74
		20x95
		32x60
		5x40
		38x40

		translate 100x0
		polygon 
		50x5
		95x28
		95x72
		50x95
		5x72
		5x28
		
		translate 100x0
		polygon 14x59 17x85 92x57 94x32 52x38 59x15
		
		translate 100x0
		polygon 
		5x15 15x5
		5x35 35x5
		5x55 55x5
		5x75 75x5
		5x95 95x5
		15x95 95x15
		35x95 95x35
		55x95 95x55
		75x95 95x75
		85x95 95x85		

		;SPLINE
		translate 100x0
		spline 87x71 86x28 50x9 15x30 17x69 50x86 80x67 79x33 50x17 23x34
		24x64 50x78 73x63 71x37 50x26 30x38 31x60 50x69 65x59 64x41 49x34 37x42 38x56 50x61 58x54 57x45
 		50x42 44x47 46x52 50x53 51x50 50x49

		translate 100x0
		spline 2 87x71 86x28 50x9 15x30 17x69 50x86 80x67 79x33 50x17 23x34
		24x64 50x78 73x63 71x37 50x26 30x38 31x60 50x69 65x59 64x41 49x34 37x42 38x56 50x61 58x54 57x45
 		50x42 44x47 46x52 50x53 51x50 50x49

		translate 100x0
		spline 3 87x71 86x28 50x9 15x30 17x69 50x86 80x67 79x33 50x17 23x34
		24x64 50x78 73x63 71x37 50x26 30x38 31x60 50x69 65x59 64x41 49x34 37x42 38x56 50x61 58x54 57x45
 		50x42 44x47 46x52 50x53 51x50 50x49

		translate 100x0
		spline 5 87x71 86x28 50x9 15x30 17x69 50x86 80x67 79x33 50x17 23x34
		24x64 50x78 73x63 71x37 50x26 30x38 31x60 50x69 65x59 64x41 49x34 37x42 38x56 50x61 58x54 57x45
 		50x42 44x47 46x52 50x53 51x50 50x49 closed

		;TRIANGLE
		reset-matrix
		translate 0x700
		triangle 50x5 95x95

		translate 100x0
		triangle 50x5 95x95 5x95

		translate 100x0
		triangle 50x5 95x95 5x95 red

		translate 100x0
		triangle 50x5 95x95 5x95 green 2

		translate 100x0
		triangle 50x5 95x95 5x95 blue -2

		translate 100x0
		triangle 50x5 95x95 5x95 red green

		translate 100x0
		triangle 50x5 95x95 5x95 red green blue

		translate 100x0
		triangle 50x5 95x95 5x95 255.0.0.200 0.255.0.200 0.0.255.200

		;SHAPE
		;TEXT
] copy []

print "Generating DRAW graphics.."
img: make image! [800x800 255.255.255]
write %draw.png encode 'png draw img draw-block
print "Output has been succesfully written to draw.png file."
halt
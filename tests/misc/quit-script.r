Rebol [
   title: "Quit With Exit Code 3"
   description: --{
       Used by GitHub Actions to make sure QUIT with exit code is honored.
   }--
]

print "Quitting With Exit Code 3"
quit 3

Rebol [
   title: "FAIL with an error"
   description: --{
       Used by GitHub Actions to make sure FAIL gives a nonzero exit code.
   }--
]

print "FAILing with a message (should give exit code 1)"
fail {The Error Message}

Rebol [
   Title: {FAIL with an error}
   Description: {
       Used by GitHub Actions to make sure FAIL gives a nonzero exit code.
   }
]

print "FAILing with a message (should give exit code 1)"
fail {The Error Message}

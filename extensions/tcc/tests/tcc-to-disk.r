REBOL [
    Title: {TCC Output of Files on Disk Test}
    Description: {
        The initial integration of TCC only allowed for in-memory compilation
        of a string of C test as the implementation of a function that Rebol
        could call.  It was later extended to allow the creation of OBJ and
        EXE files on disk.

        The C99 command is based on the POSIX standard for command lines:

        http://pubs.opengroup.org/onlinepubs/9699919799/utilities/c99.html

        It builds a dialected block which it passes to the usermode COMPILE
        front end, which translates that block into an object suitable for
        the lower-level COMPILE* command written in C that speaks to libtcc.
    }
]

write %hello-tcc.c trim/auto {
    /*
     * hello-tcc.c
     * Simple test of compiling a file from disk w/TCC extension
     */

    #include <stdio.h>

    int main(int argc, char *argv[]) {
        printf("Hello, TCC Disk File World!\n");
        return 0;
    }
}

c99-logged: enclose :c99 function [f [frame!]] [
    ; f.runtime: "..."  ; set this to override CONFIG_TCCDIR

    fdebug: copy f
    fdebug.inspect: true
    do fdebug  ; Run command once with /INSPECT (don't compile)

    return do f  ; Run original command without /INSPECT
]

print "== ONE STEP COMPILATION (direct to executable) =="
c99-logged "hello-tcc.c -o -DMAIN=main hello-tcc-onestep"

print "== TWO STEP COMPILATION (`.o` file, then make executable from that) =="
c99-logged "hello-tcc.c -c -o hello-tcc-renamed.o -DMAIN=main"
c99-logged "hello-tcc-renamed.o -o hello-tcc-twostep"

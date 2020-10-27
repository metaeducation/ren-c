## DIRECTORY FOR DOWNLOADING PRE-BUILT BOOTSTRAP REBOL EXECUTABLES TO

By convention, this `prebuilt` directory is where Rebol executables are kept
which are able to do the configuration and C code generation to do a 
bootstrap build.

Important to realize about the bootstrapping strategy is that there are only
two builds that are supposed to be "guaranteed" to work to build the code
in any moment: the "stable" bootstrap build, and the current commit.  So while
that although you can build the current codebase with itself, it might stop
working at any point.  This is because the "shim" which is used to ensure the
build tools run only targets one version.

The bootstrap commit is currently at git commit short hash: 8994d23


### DOWNLOAD LOCATIONS

At one point the executables were committed directly into the Git repository.
This was decided to be undesirable.  So instead, the files are now stored on
an S3 instance.  Having the build process get these files automatically is a
goal, but in the meantime, these are the current links:

LINUX:
https://s3.amazonaws.com/r3bootstraps/r3-linux-x64-8994d23

WINDOWS:
https://s3.amazonaws.com/r3bootstraps/r3-windows-x86-8994d23.exe

OS X:
https://s3.amazonaws.com/r3bootstraps/r3-osx-x64-8994d23

ANDROID:
https://s3.amazonaws.com/r3bootstraps/r3-android-arm-8994d23

OPENBSD: (submitted by Stéphane Aulery)
https://s3.amazonaws.com/r3bootstraps/r3-openbsd-x64-8994d23

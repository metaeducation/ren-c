This cypress test is special, because the e2e directory starts out empty, but it
gathers tests out of URLs mentioned in %e2e/README.md

See %web-build.yml in the .github/workflows/ folder for the code that does this.

When you update the package.json, you need to also update package-lock.json

This has to be done with an installation of NPM.  Use the command:

    npm i --package-lock-only

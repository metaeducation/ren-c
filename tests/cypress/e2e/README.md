This directory is where Cypress looks for the *.cy.js files that it will run
as part of the greenlighting tests.  We don't commit files here--but rather, the
GitHub workflow pulls them from these locations:

* https://gitlab.com/Zhaoshirong/rebol-chess/-/raw/master/cypress/e2e/chess.cy.js
* https://raw.githubusercontent.com/gchiu/midcentral/main/cypress/e2e/rx-app.cy.js

(In fact, this README.md file is parsed for those URLs using UPARSE, and the
are pulled into the directory by the workflow.)

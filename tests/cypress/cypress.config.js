// %cypress.config.js
//
// https://docs.cypress.io/guides/references/configuration#Configuration-File
///
// The way the Cypress GitHub Action works is that you pass a config file as
// a parameter.  This config contains a `specPattern` which locates the
// scripts it is supposed to run.
//
// The GitHub action is configured with `working-dir: tests/cypress/` so it
// seems to know that paths here are relative to that directory.
//

const { defineConfig } = require('cypress')

module.exports = defineConfig({
  projectId: "wqxv1u",  // metaeducation @ cypress.io

  defaultCommandTimeout: 15000,

  e2e: {
    specPattern: 'e2e/**/*.cy.{js,jsx,ts,tsx}',
    supportFile: 'support/e2e.{js,jsx,ts,tsx}',

    setupNodeEvents(on, config) {
      // implement node event listeners here
    },
  },
})

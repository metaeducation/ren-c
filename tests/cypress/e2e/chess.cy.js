//
// %chess.cy.js
//

let replpad_url = 'http://hostilefork.com/media/shared/replpad-js/'

let short_hash = Cypress.env('GIT_COMMIT_SHORT')
if (short_hash)
    replpad_url += '?git_commit=' + short_hash

describe('Chess App on replpad continuous integration', () => {
  it('Visits Replpad', () => {
    cy.visit(replpad_url)
    cy.get('.input').type('do @chess{enter}')
    cy.wait(5000)
    let el = cy.get('.input:focus')
    el.type('chiu-vs-jensen{enter}')
    // cy.contains('chess>>').type('chiu-vs-jensen{enter}')
    cy.wait(30000)
    cy.contains('Thanks Kai!')
  })
})

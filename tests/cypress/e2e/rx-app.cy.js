//
// %rx-app.cy.js
//

let replpad_url = 'http://hostilefork.com/media/shared/replpad-js/'

let short_hash = Cypress.env('GIT_COMMIT_SHORT')
if (short_hash)
    replpad_url += '?git_commit=' + short_hash

describe('Rx replpad App continous integration', () => {
  it('Visits Replpad', () => {
    cy.visit(replpad_url)
    cy.get('.input').type('import @rx{enter}')
    cy.get('.stdout').should('contain','Enter your name as appears on a prescription:')
    cy.get('.input:focus').type('Graham Chiu{enter}')
    cy.get('.input:focus').type('1234567{enter}')
    cy.get('.input:focus').type('y{enter}')
    cy.get('.input:focus').type('n{enter}')
    cy.get('.input:focus').type('ABC1234{enter}')
    cy.get('.input:focus').type('Mr{enter}')
    cy.get('.input:focus').type('Foo{enter}')
    cy.get('.input:focus').type('Basil{enter}')
    cy.get('.input:focus').type('1-Jan-1920{enter}')
    cy.get('.input:focus').type('88 Baker Street{enter}')
    cy.get('.input:focus').type('Kensington{enter}')
    cy.get('.input:focus').type('London{enter}')
    cy.get('.input:focus').type('04 123456{enter}')
    cy.get('.input:focus').type('Male{enter}')
    cy.get('.input:focus').type('y{enter}')
    cy.get('.input:focus').type("rx 'mtx{enter}")
    cy.contains('Which schedule to use?').next().type('1{enter}')
    cy.wait(2000)
    cy.get('.line').last().find('.input').type('write-rx{enter}')
    cy.contains('For email?').next().type('y{enter}')
  })
})

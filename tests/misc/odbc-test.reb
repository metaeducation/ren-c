Rebol [
    Title: "ODBC Test Script"
    Description: {
        This script does some basic table creation, assuming you have
        configured an ODBC connection with the DSN "Rebol" that has a "test"
        database inside it.  Then it queries to make sure it can get the
        data back out.
    }
]

tables: [
    bit "BIT" [#[false] #[true]]

    tinyint_s "TINYINT" [-128 -10 0 10 127]
    tinyint_u "TINYINT UNSIGNED" [0 10 20 30 255]
    smallint_s "SMALLINT" [-32768 -10 0 10 32767]
    smallint_u "SMALLINT UNSIGNED" [0 10 20 30 65535]
    integer_s "INT" [-2147483648 -10 0 10 2147483647]
    integer_u "INT UNSIGNED" [0 10 20 30 4294967295]
    bigint_s "BIGINT" [-9223372036854775808 -10 0 10 9223372036854775807]
    ;
    ; Note: though BIGINT unsigned storage in ODBC can store the full range of
    ; unsigned 64-bit values, Rebol's INTEGER! is always signed.  Hence it
    ; is limited to the signed range.
    ;
    bigint_u "BIGINT UNSIGNED" [0 10 20 30 9223372036854775807]

    real "REAL" [-3.4 -1.2 0.0 5.6 7.8]
    double "DOUBLE" [-3.4 -1.2 0.0 5.6 7.8]
    float "FLOAT(20)" [-3.4 -1.2 0.0 5.6 7.8]
    numeric "NUMERIC(20,2)" [-3.4 -1.2 0.0 5.6 7.8]
    decimal "DECIMAL(3,2)" [-3.4 -1.2 0.0 5.6 7.8]

    date "DATE" [12-Dec-2012 21-Apr-1975]
    time "TIME" [10:20 3:04]
    timestamp "TIMESTAMP" [30-May-2017/14:23:08 12-Dec-2012]

    char "CHAR(3)" ["abc" "def" "ghi"]
    varchar "VARCHAR(10)" ["" "abc" "defgh" "jklmnopqrs"]

    nchar "NCHAR(3)" ["abc" "ταБ" "ghi"]
    nvarchar "NVARCHAR(10)" ["" "abc" "ταБЬℓσ" "٩(●̮̮̃•̃)۶"]

    binary "BINARY(3)" [#{000000} #{010203} #{FFFFFF}]
    varbinary "VARBINARY(10)" [#{} #{010203} #{DECAFBADCAFE}]
    blob "BLOB(10)" [#{} #{010203} #{DECAFBADCAFE}]
]

connection: open odbc://Rebol
statement: first connection

for-each [name sqltype content] tables [
    ;
    ; Drop table if it exists
    ;
    trap [
        insert statement unspaced [
            {DROP TABLE `test`.`} name {`}
        ]
    ]

    ; Create table, each one of which has a single field `value` as the
    ; primary key, of the named type.
    ;
    insert statement unspaced [
        {CREATE TABLE `test`.`} name {` (}
            {id INT NOT NULL AUTO_INCREMENT,}
            {`value`} space sqltype space {NOT NULL,}
            {PRIMARY KEY (`id`))}
    ]

    ; Insert the values.  As a side effect, here we wind up testing the
    ; parameter code for each type.
    ;
    for-each value content [
        insert statement reduce [
            unspaced [{INSERT INTO `test`.`} name {` (`value`) VALUES ( ? )}]
            value
        ]
    ]

    ; Query the rows and make sure the values that come back are the same
    ;
    insert statement unspaced [
        {SELECT value FROM `test`.`} name {`}
    ]

    rows: copy statement
    actual: copy []
    for-each row rows [
        assert [1 == length of row]
        append actual first row
    ]

    print mold actual
    print mold content

    either (sort copy actual) == (sort copy content) [
        print "QUERY MATCHED ORIGINAL DATA"
    ][
        print "QUERY DID NOT MATCH ORIGINAL DATA"
    ]

    print-newline
]

close statement
close connection

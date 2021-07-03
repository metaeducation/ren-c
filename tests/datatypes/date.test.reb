; datatypes/date.r

(date? 25-Sep-2006)
(path? '25/Sep/2006)  ; This was a DATE in R3-Alpha, but now it's a PATH!

(not date? 1)
(date! = type of 25-Sep-2006)

; alternative formats
(25-Sep-2006 = 25-9-2006)
(25-Sep-2006 = make date! "25/Sep/2006")
(25-Sep-2006 = to date! "25-Sep-2006")
("25-Sep-2006" = mold 25-Sep-2006)

; minimum
(date? 1-Jan-0000)

; another minimum
(date? 1-Jan-0000/0:00)

; extreme behaviour
(
    did any [
        error? trap [date-d: 1-Jan-0000 - 1]
        date-d = load-value mold date-d
    ]
)
(
    did any [
        error? trap [date-d: 31-Dec-16383 + 1]
        date-d = load-value mold date-d
    ]
)

[#1250 (
    did all [
        error? trap [load "1/11--00"]
        error? trap [load "1/11--0"]
        (load-value "1-11-0") = (load-value "1-11-00")
    ]
)]

[#213 (
    d: 28-Mar-2019/17:25:40-4:00
    d: d/date
    (d + 1) == 29-Mar-2019
)]

[https://github.com/red/red/issues/3881 (
    d: 29-Feb-2020
    d/year: d/year - 1
    d = 1-Mar-2019
)]

[#1637 (
    d: now/date
    did all [
        null? :d/time
        null? :d/zone
    ]
)]

(
    [d n]: transcode "1975-04-21/10:20:03.04"
    did all [
        date? d
        n = ""
        d/year = 1975
        d/month = 4
        d/day = 21
        d/hour = 10
        d/minute = 20
        d/second = 3.04
    ]
)

(2020-11-24 < 2020-11-25)
(not 2020-11-24 > 2020-11-25)


; @codebybrett wrote these date tests in a forum post.
; https://forum.rebol.info/t/240/6
[
    (
        date-111: 7-aug-2017/03:00+10:00  ; DateTimeZone
        date-110: 7-aug-2017/03:00  ; DateTime
        date-100: 7-aug-2017  ; DateOnly
        true
    )

    ;
    ; Basic date tests.

    (d: date-111 same? d date-111)
    (d: date-110 same? d date-110)
    (d: date-100 same? d date-100)

    (same? date-111/date date-100)
    (same? date-110/date date-100)
    (same? date-100/date date-100)
    (same? date-111/date date-100/date)
    (same? date-111/time date-110/time)

    (null <> date-111/time)
    (null <> date-110/time)
    (null <> date-111/zone)
    (null = date-110/zone)
    (null = date-100/zone)
    (null = date-100/time)

    (not equal? date-110 date-100)
    (not equal? date-111 date-100)

    ;
    ; Math

    (0 = subtract date-111 date-111)
    (0 = subtract date-110 date-110)
    (0 = subtract date-100 date-100)
    ;('invalid-compare = pick trap [subtract date-111 date-110] 'id)
    ;('invalid-compare = pick trap [subtract date-110 date-100] 'id)

    (0:00 = difference date-111 date-111)
    (0:00 = difference date-110 date-110)
    ;('invalid-compare = pick trap [difference date-111 date-110] 'id)
    ;('invalid-compare = pick trap [difference date-110 date-100] 'id)
    ;('invalid-compare = pick trap [difference date-100 date-100] 'id)

    (date-100 <= date-100)
    (date-110 <= date-110)
    (date-111 <= date-111)
    ('invalid-compare = pick trap [date-111 <= date-110] 'id)
    ('invalid-compare = pick trap [date-110 <= date-100] 'id)

    ;
    ; Mappings

    (date-111/utc/zone = 0:00)
    (error? trap [date-110/utc])
    (error? trap [date-100/utc])

    ; Brett had written this test assuming there was a /LOCAL refinement on
    ; dates, but this does not exist in Rebol2, R3-Alpha, or Red.  We assume
    ; that it should take a date which has a time zone on it, adjust the time
    ; for the user's local timezone, and remove the zone.  But if this is the
    ; interpretation it doesn't make sense why date-110 would not be willing
    ; to convert to UTC (?)
    ;
    ; (error? [date-111/local/zone])

    ; !!! This is proposed behavior for MAKE of one date from another, that
    ; has not been implemented.
    ;
    ; (
    ;    d: make date-110 [zone: date-111/zone]
    ;    same? d date-111
    ; )(
    ;    d: make date-100 [time: date-110/time]
    ;    same? d date-110
    ; )(
    ;    d: make date-100 [time: date-111/time zone: date-111/zone]
    ;    same? d date-111
    ; )

    ;
    ; Date field mutation tests.

    (  ; Clear zone - no date adjustment.
        d: date-111
        d/zone: null
        same? d date-110
    )

    (  ; Clear time and zone - no date adjustment.
        d: date-111
        d/time: null
        same? d date-100
    )

    (  ; Set time - no adjustment performed.
        d: date-111
        d/time: d/time + 0:00
        same? d date-111
    )

    (  ; Set time - no adjustment performed.
        d: date-110
        d/time: d/time + 0:00
        same? d date-110
    )

    (  ; Set time.
        d: date-100
        d/time: date-110/time
        same? d date-110
    )

    (  ; Set zone.
        d: date-111
        d/zone: date-111/zone
        same? d date-111
    )

    (  ; Set zone.
        d: date-110
        d/zone: date-111/zone
        same? d date-111
    )

    (  ; Set zone without time.
        d: date-100
        error? trap [d/zone: 10:00]
    )
]

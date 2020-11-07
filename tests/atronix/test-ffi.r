REBOL []
recycle/torture
libc: make library! %libc.so.6
;fopen: make routine! compose [library: (libc) name: "fopen" return: 'pointer [pointer pointer]]
;fclose: make routine! compose [library: (libc) name: "fclose" return: 'int32 [pointer]]
;fwrite: make routine! compose [library: (libc) name: "fwrite" return: 'int64 [pointer int64 int64 pointer]]
fopen: make routine! compose [[ path [pointer] mode [pointer] return: [pointer]] (libc) "fopen"]

fclose: make routine! compose [[ fp [pointer] return: [int32] ] (libc) "fclose"]

fwrite: make routine! compose [[ ptr [pointer] size [int64] nmemb [int64] stream [int64] return: [int64] ] (libc) "fwrite"]

fread: make routine! compose [[ ptr [pointer] size [int64] nmemb [int64] stream [int64] return: [int64] ] (libc) "fread"]

fseek: make routine! compose [[ fp [pointer] offset [int64] where [int32] return: [int32]] (libc) "fseek"]

fp: fopen "/tmp/test.txt" "w+"
hello: "hello world"
fwrite hello length of hello 1 fp

buf: make struct! [s [uint8 [128]]]
fseek fp 0 0
fread (reflect buf 'addr) length of buf 1 fp
print ["read:" to text! values of buf "(" values of buf ")"]

a: "XXXXXXXXXXXXXX"
fseek fp 0 0
fread a length of a 1 fp
print ["read:" a]
fclose fp

;struct tm {
;           int tm_sec;    /* Seconds (0-60) */
;           int tm_min;    /* Minutes (0-59) */
;           int tm_hour;   /* Hours (0-23) */
;           int tm_mday;   /* Day of the month (1-31) */
;           int tm_mon;    /* Month (0-11) */
;           int tm_year;   /* Year - 1900 */
;           int tm_wday;   /* Day of the week (0-6, Sunday = 0) */
;           int tm_yday;   /* Day in the year (0-365, 1 Jan = 0) */
;           int tm_isdst;  /* Daylight saving time */
;       };

tm: make struct! [
    tm_sec [int32]
    tm_min [int32]
    tm_hour [int32] ;   /* Hours (0-23) */
    tm_mday [int32] ;   /* Day of the month (1-31) */
    tm_mon  [int32] ;    /* Month (0-11) */
    tm_year [int32] ;   /* Year - 1900 */
    tm_wday [int32] ;   /* Day of the week (0-6, Sunday = 0) */
    tm_yday [int32] ;   /* Day in the year (0-365, 1 Jan = 0) */
    tm_isdst [int32];  /* Daylight saving time */
]
time_t: make struct! [
    t [int64]
]

time: make routine! compose [[t [pointer] return: [int64]] (libc) "time"]
print ["time:" time (reflect time_t 'addr)]
localtime_r: make routine! compose [[t [pointer] tm [pointer] return: [int64]] (libc) "localtime_r"]

print ["localtime:" localtime_r, reflect time_t 'addr, reflect tm 'addr]

print ["tm:" mold tm]

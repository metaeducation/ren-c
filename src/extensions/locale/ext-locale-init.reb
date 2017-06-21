REBOL [
    Title: "Locale Extension"
    name: 'Locale
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]


unless 'Windows = first system/platform [
    ; Windows has locale implemented as a native

    ;DO NOT EDIT this table
    ;It's updated by iso3166.r
    iso-3166-table: make map! lock [
    "AF" "Afghanistan"
    "AX" "Åland Islands"
    "AL" "Albania"
    "DZ" "Algeria"
    "AS" "American Samoa"
    "AD" "Andorra"
    "AO" "Angola"
    "AI" "Anguilla"
    "AQ" "Antarctica"
    "AG" "Antigua And Barbuda"
    "AR" "Argentina"
    "AM" "Armenia"
    "AW" "Aruba"
    "AU" "Australia"
    "AT" "Austria"
    "AZ" "Azerbaijan"
    "BS" "Bahamas"
    "BH" "Bahrain"
    "BD" "Bangladesh"
    "BB" "Barbados"
    "BY" "Belarus"
    "BE" "Belgium"
    "BZ" "Belize"
    "BJ" "Benin"
    "BM" "Bermuda"
    "BT" "Bhutan"
    "BO" "Bolivia, Plurinational State of"
    "BQ" "Bonaire, Sint Eustatius And Saba"
    "BA" "Bosnia And Herzegovina"
    "BW" "Botswana"
    "BV" "Bouvet Island"
    "BR" "Brazil"
    "IO" "British Indian Ocean Territory"
    "BN" "Brunei Darussalam"
    "BG" "Bulgaria"
    "BF" "Burkina Faso"
    "BI" "Burundi"
    "KH" "Cambodia"
    "CM" "Cameroon"
    "CA" "Canada"
    "CV" "Cape Verde"
    "KY" "Cayman Islands"
    "CF" "Central African Republic"
    "TD" "Chad"
    "CL" "Chile"
    "CN" "China"
    "CX" "Christmas Island"
    "CC" "Cocos (keeling) Islands"
    "CO" "Colombia"
    "KM" "Comoros"
    "CG" "Congo"
    "CD" "Congo, The Democratic Republic of The"
    "CK" "Cook Islands"
    "CR" "Costa Rica"
    "CI" "Côte D'ivoire"
    "HR" "Croatia"
    "CU" "Cuba"
    "CW" "Curaçao"
    "CY" "Cyprus"
    "CZ" "Czech Republic"
    "DK" "Denmark"
    "DJ" "Djibouti"
    "DM" "Dominica"
    "DO" "Dominican Republic"
    "EC" "Ecuador"
    "EG" "Egypt"
    "SV" "El Salvador"
    "GQ" "Equatorial Guinea"
    "ER" "Eritrea"
    "EE" "Estonia"
    "ET" "Ethiopia"
    "FK" "Falkland Islands (malvinas)"
    "FO" "Faroe Islands"
    "FJ" "Fiji"
    "FI" "Finland"
    "FR" "France"
    "GF" "French Guiana"
    "PF" "French Polynesia"
    "TF" "French Southern Territories"
    "GA" "Gabon"
    "GM" "Gambia"
    "GE" "Georgia"
    "DE" "Germany"
    "GH" "Ghana"
    "GI" "Gibraltar"
    "GR" "Greece"
    "GL" "Greenland"
    "GD" "Grenada"
    "GP" "Guadeloupe"
    "GU" "Guam"
    "GT" "Guatemala"
    "GG" "Guernsey"
    "GN" "Guinea"
    "GW" "Guinea-bissau"
    "GY" "Guyana"
    "HT" "Haiti"
    "HM" "Heard Island And Mcdonald Islands"
    "VA" "Holy See (vatican City State)"
    "HN" "Honduras"
    "HK" "Hong Kong"
    "HU" "Hungary"
    "IS" "Iceland"
    "IN" "India"
    "ID" "Indonesia"
    "IR" "Iran, Islamic Republic of"
    "IQ" "Iraq"
    "IE" "Ireland"
    "IM" "Isle of Man"
    "IL" "Israel"
    "IT" "Italy"
    "JM" "Jamaica"
    "JP" "Japan"
    "JE" "Jersey"
    "JO" "Jordan"
    "KZ" "Kazakhstan"
    "KE" "Kenya"
    "KI" "Kiribati"
    "KP" "Korea, Democratic People's Republic of"
    "KR" "Korea, Republic of"
    "KW" "Kuwait"
    "KG" "Kyrgyzstan"
    "LA" "Lao People's Democratic Republic"
    "LV" "Latvia"
    "LB" "Lebanon"
    "LS" "Lesotho"
    "LR" "Liberia"
    "LY" "Libya"
    "LI" "Liechtenstein"
    "LT" "Lithuania"
    "LU" "Luxembourg"
    "MO" "Macao"
    "MK" "Macedonia, The Former Yugoslav Republic of"
    "MG" "Madagascar"
    "MW" "Malawi"
    "MY" "Malaysia"
    "MV" "Maldives"
    "ML" "Mali"
    "MT" "Malta"
    "MH" "Marshall Islands"
    "MQ" "Martinique"
    "MR" "Mauritania"
    "MU" "Mauritius"
    "YT" "Mayotte"
    "MX" "Mexico"
    "FM" "Micronesia, Federated States of"
    "MD" "Moldova, Republic of"
    "MC" "Monaco"
    "MN" "Mongolia"
    "ME" "Montenegro"
    "MS" "Montserrat"
    "MA" "Morocco"
    "MZ" "Mozambique"
    "MM" "Myanmar"
    "NA" "Namibia"
    "NR" "Nauru"
    "NP" "Nepal"
    "NL" "Netherlands"
    "NC" "New Caledonia"
    "NZ" "New Zealand"
    "NI" "Nicaragua"
    "NE" "Niger"
    "NG" "Nigeria"
    "NU" "Niue"
    "NF" "Norfolk Island"
    "MP" "Northern Mariana Islands"
    "NO" "Norway"
    "OM" "Oman"
    "PK" "Pakistan"
    "PW" "Palau"
    "PS" "Palestine, State of"
    "PA" "Panama"
    "PG" "Papua New Guinea"
    "PY" "Paraguay"
    "PE" "Peru"
    "PH" "Philippines"
    "PN" "Pitcairn"
    "PL" "Poland"
    "PT" "Portugal"
    "PR" "Puerto Rico"
    "QA" "Qatar"
    "RE" "Réunion"
    "RO" "Romania"
    "RU" "Russian Federation"
    "RW" "Rwanda"
    "BL" "Saint Barthélemy"
    "SH" "Saint Helena, Ascension And Tristan Da Cunha"
    "KN" "Saint Kitts And Nevis"
    "LC" "Saint Lucia"
    "MF" "Saint Martin (french Part)"
    "PM" "Saint Pierre And Miquelon"
    "VC" "Saint Vincent And The Grenadines"
    "WS" "Samoa"
    "SM" "San Marino"
    "ST" "Sao Tome And Principe"
    "SA" "Saudi Arabia"
    "SN" "Senegal"
    "RS" "Serbia"
    "SC" "Seychelles"
    "SL" "Sierra Leone"
    "SG" "Singapore"
    "SX" "Sint Maarten (dutch Part)"
    "SK" "Slovakia"
    "SI" "Slovenia"
    "SB" "Solomon Islands"
    "SO" "Somalia"
    "ZA" "South Africa"
    "GS" "South Georgia And The South Sandwich Islands"
    "SS" "South Sudan"
    "ES" "Spain"
    "LK" "Sri Lanka"
    "SD" "Sudan"
    "SR" "Suriname"
    "SJ" "Svalbard And Jan Mayen"
    "SZ" "Swaziland"
    "SE" "Sweden"
    "CH" "Switzerland"
    "SY" "Syrian Arab Republic"
    "TW" "Taiwan, Province of China"
    "TJ" "Tajikistan"
    "TZ" "Tanzania, United Republic of"
    "TH" "Thailand"
    "TL" "Timor-leste"
    "TG" "Togo"
    "TK" "Tokelau"
    "TO" "Tonga"
    "TT" "Trinidad And Tobago"
    "TN" "Tunisia"
    "TR" "Turkey"
    "TM" "Turkmenistan"
    "TC" "Turks And Caicos Islands"
    "TV" "Tuvalu"
    "UG" "Uganda"
    "UA" "Ukraine"
    "AE" "United Arab Emirates"
    "GB" "United Kingdom"
    "US" "United States"
    "UM" "United States Minor Outlying Islands"
    "UY" "Uruguay"
    "UZ" "Uzbekistan"
    "VU" "Vanuatu"
    "VE" "Venezuela, Bolivarian Republic of"
    "VN" "Viet Nam"
    "VG" "Virgin Islands, British"
    "VI" "Virgin Islands, U.S."
    "WF" "Wallis And Futuna"
    "EH" "Western Sahara"
    "YE" "Yemen"
    "ZM" "Zambia"
    "ZW" "Zimbabwe"
]

    ;DO NOT EDIT this table
    ;It's updated by iso639.r
    iso-639-table: make map! lock [
    "aa" "Afar"
    "ab" "Abkhazian"
    "af" "Afrikaans"
    "ak" "Akan"
    "sq" "Albanian"
    "am" "Amharic"
    "ar" "Arabic"
    "an" "Aragonese"
    "hy" "Armenian"
    "as" "Assamese"
    "av" "Avaric"
    "ae" "Avestan"
    "ay" "Aymara"
    "az" "Azerbaijani"
    "ba" "Bashkir"
    "bm" "Bambara"
    "eu" "Basque"
    "be" "Belarusian"
    "bn" "Bengali"
    "bh" "Bihari languages"
    "bi" "Bislama"
    "bs" "Bosnian"
    "br" "Breton"
    "bg" "Bulgarian"
    "my" "Burmese"
    "ca" "Catalan; Valencian"
    "ch" "Chamorro"
    "ce" "Chechen"
    "zh" "Chinese"
    "cu" {Church Slavic; Old Slavonic; Church Slavonic; Old Bulgarian; Old Church Slavonic}
    "cv" "Chuvash"
    "kw" "Cornish"
    "co" "Corsican"
    "cr" "Cree"
    "cs" "Czech"
    "da" "Danish"
    "dv" "Divehi; Dhivehi; Maldivian"
    "nl" "Dutch; Flemish"
    "dz" "Dzongkha"
    "en" "English"
    "eo" "Esperanto"
    "et" "Estonian"
    "ee" "Ewe"
    "fo" "Faroese"
    "fj" "Fijian"
    "fi" "Finnish"
    "fr" "French"
    "fy" "Western Frisian"
    "ff" "Fulah"
    "ka" "Georgian"
    "de" "German"
    "gd" "Gaelic; Scottish Gaelic"
    "ga" "Irish"
    "gl" "Galician"
    "gv" "Manx"
    "el" "Greek, Modern (1453-)"
    "gn" "Guarani"
    "gu" "Gujarati"
    "ht" "Haitian; Haitian Creole"
    "ha" "Hausa"
    "he" "Hebrew"
    "hz" "Herero"
    "hi" "Hindi"
    "ho" "Hiri Motu"
    "hr" "Croatian"
    "hu" "Hungarian"
    "ig" "Igbo"
    "is" "Icelandic"
    "io" "Ido"
    "ii" "Sichuan Yi; Nuosu"
    "iu" "Inuktitut"
    "ie" "Interlingue; Occidental"
    "ia" {Interlingua (International Auxiliary Language Association)}
    "id" "Indonesian"
    "ik" "Inupiaq"
    "it" "Italian"
    "jv" "Javanese"
    "ja" "Japanese"
    "kl" "Kalaallisut; Greenlandic"
    "kn" "Kannada"
    "ks" "Kashmiri"
    "kr" "Kanuri"
    "kk" "Kazakh"
    "km" "Central Khmer"
    "ki" "Kikuyu; Gikuyu"
    "rw" "Kinyarwanda"
    "ky" "Kirghiz; Kyrgyz"
    "kv" "Komi"
    "kg" "Kongo"
    "ko" "Korean"
    "kj" "Kuanyama; Kwanyama"
    "ku" "Kurdish"
    "lo" "Lao"
    "la" "Latin"
    "lv" "Latvian"
    "li" "Limburgan; Limburger; Limburgish"
    "ln" "Lingala"
    "lt" "Lithuanian"
    "lb" "Luxembourgish; Letzeburgesch"
    "lu" "Luba-Katanga"
    "lg" "Ganda"
    "mk" "Macedonian"
    "mh" "Marshallese"
    "ml" "Malayalam"
    "mi" "Maori"
    "mr" "Marathi"
    "ms" "Malay"
    "mg" "Malagasy"
    "mt" "Maltese"
    "mn" "Mongolian"
    "na" "Nauru"
    "nv" "Navajo; Navaho"
    "nr" "Ndebele, South; South Ndebele"
    "nd" "Ndebele, North; North Ndebele"
    "ng" "Ndonga"
    "ne" "Nepali"
    "nn" "Norwegian Nynorsk; Nynorsk, Norwegian"
    "nb" "Bokmål, Norwegian; Norwegian Bokmål"
    "no" "Norwegian"
    "ny" "Chichewa; Chewa; Nyanja"
    "oc" "Occitan (post 1500); Provençal"
    "oj" "Ojibwa"
    "or" "Oriya"
    "om" "Oromo"
    "os" "Ossetian; Ossetic"
    "pa" "Panjabi; Punjabi"
    "fa" "Persian"
    "pi" "Pali"
    "pl" "Polish"
    "pt" "Portuguese"
    "ps" "Pushto; Pashto"
    "qu" "Quechua"
    "rm" "Romansh"
    "ro" "Romanian; Moldavian; Moldovan"
    "rn" "Rundi"
    "ru" "Russian"
    "sg" "Sango"
    "sa" "Sanskrit"
    "si" "Sinhala; Sinhalese"
    "sk" "Slovak"
    "sl" "Slovenian"
    "se" "Northern Sami"
    "sm" "Samoan"
    "sn" "Shona"
    "sd" "Sindhi"
    "so" "Somali"
    "st" "Sotho, Southern"
    "es" "Spanish; Castilian"
    "sc" "Sardinian"
    "sr" "Serbian"
    "ss" "Swati"
    "su" "Sundanese"
    "sw" "Swahili"
    "sv" "Swedish"
    "ty" "Tahitian"
    "ta" "Tamil"
    "tt" "Tatar"
    "te" "Telugu"
    "tg" "Tajik"
    "tl" "Tagalog"
    "th" "Thai"
    "bo" "Tibetan"
    "ti" "Tigrinya"
    "to" "Tonga (Tonga Islands)"
    "tn" "Tswana"
    "ts" "Tsonga"
    "tk" "Turkmen"
    "tr" "Turkish"
    "tw" "Twi"
    "ug" "Uighur; Uyghur"
    "uk" "Ukrainian"
    "ur" "Urdu"
    "uz" "Uzbek"
    "ve" "Venda"
    "vi" "Vietnamese"
    "vo" "Volapük"
    "cy" "Welsh"
    "wa" "Walloon"
    "wo" "Wolof"
    "xh" "Xhosa"
    "yi" "Yiddish"
    "yo" "Yoruba"
    "za" "Zhuang; Chuang"
    "zu" "Zulu"
]

    hijack 'locale function [
        {parameter of 'language/language* or 'territory/territory* returns a value from the ISO tables}
        type [word!]
        <has>
        iso-639 (iso-639-table)
        iso-3166 (iso-3166-table)
        error-msg
    ][
        env-lang: get-env "LANG"
        error-msg: spaced ["*** Unrecognised env LANG:" env-lang " - defaulting to US English ***"]
        unless env-lang [
            return _
        ]

        letter: charset [#"a" - #"z" #"A" - #"Z"]
        unless parse env-lang [
            copy lang: [some letter]
            #"_" copy territory: [some letter]
            to end
        ][
            if lang <> "C" [print error-msg]
            lang: "en" territory: "US"
        ]

         case [
            find? [language language*] type [
                unless lang: select iso-639 lang [print error-msg]
                lang: default "en"
            ]
            find? [territory territory*] type [
                unless territory: select iso-3166 territory [print error-msg]
                territory: default "US"
            ]
            true [
                fail unspaced ["Unrecognised env LANG value" env-lang]
            ]
        ]
    ]

    unset 'iso-3166-table
    unset 'iso-639-table
]

; initialize system/locale
system/locale/language: locale 'language
system/locale/language*: locale 'language*
system/locale/locale: locale 'territory
system/locale/locale*: locale 'territory*

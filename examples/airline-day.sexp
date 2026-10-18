; A day of a small airline, made up, for examples/airline.slight: what it
; planned, what the weather did, and the record of what flew. Times are
; minutes after midnight; the hub is ORD.

; Each aircraft, and where it starts the day.
(:aircraft "N101" "BOS")
(:aircraft "N102" "ATL")
(:aircraft "N103" "DEN")
(:aircraft "N104" "ORD")
(:aircraft "N105" "ORD")

; The schedule: flight, aircraft, from, to, departure, block minutes,
; passengers. Each aircraft's legs are in the order it flies them.
(:leg "SL101" "N101" "BOS" "ORD"  360 150 142)
(:leg "SL102" "N101" "ORD" "BOS"  600 140 128)
(:leg "SL103" "N101" "BOS" "ORD"  840 150 150)
(:leg "SL104" "N101" "ORD" "BOS" 1080 140 136)
(:leg "SL201" "N102" "ATL" "ORD"  390 130 140)
(:leg "SL202" "N102" "ORD" "ATL"  600 125 118)
(:leg "SL203" "N102" "ATL" "ORD"  780 130 155)
(:leg "SL204" "N102" "ORD" "ATL"  990 125 122)
(:leg "SL301" "N103" "DEN" "ORD"  420 145 160)
(:leg "SL302" "N103" "ORD" "DEN"  660 160 148)
(:leg "SL303" "N103" "DEN" "ORD"  900 145 152)
(:leg "SL304" "N103" "ORD" "DEN" 1110 160 139)
(:leg "SL401" "N104" "ORD" "BOS"  420 130 110)
(:leg "SL402" "N104" "BOS" "ORD"  630 150 146)
(:leg "SL403" "N104" "ORD" "ATL"  840 125 131)
(:leg "SL404" "N104" "ATL" "ORD" 1035 130 127)
(:leg "SL501" "N105" "ORD" "DEN"  450 160 125)
(:leg "SL502" "N105" "DEN" "ORD"  690 145 158)
(:leg "SL503" "N105" "ORD" "BOS"  900 130 144)
(:leg "SL504" "N105" "BOS" "ORD" 1110 150 133)

; Passengers changing planes at ORD: from, to, how many.
(:connect "SL101" "SL302" 30)
(:connect "SL201" "SL102" 25)
(:connect "SL502" "SL503" 20)
(:connect "SL203" "SL304" 20)
(:connect "SL303" "SL104" 18)
(:connect "SL103" "SL304" 15)

; The weather: legs that took longer than scheduled, by how many minutes;
; and an airport closed, from and till.
(:wind "SL102" 10)
(:wind "SL303" 5)
(:wind "SL404" 8)
(:closed "DEN" 600 615)

; The what-ifs, each a branch, its parent, and its change: a flight that
; leaves late; the hub closed for two and a half hours; an aircraft out
; of service from 10:00; on top of the closure, quicker turns at the hub
; after it; and on top of the late flight, after it has rejoined the
; base, another.
(:branch "b1" "base" (:delay "SL201" 55))
(:branch "b2" "base" (:close "ORD" 690 840))
(:branch "b3" "base" (:out-of-service "N104" 600))
(:branch "b4" "b2" (:quick-turn "ORD" 780 900 20))
(:branch "b5" "b1" (:delay "SL403" 30))

; The record: when each flight left and landed.
(:flew "SL101" 360 510)
(:flew "SL102" 600 750)
(:flew "SL103" 840 990)
(:flew "SL104" 1080 1220)
(:flew "SL201" 390 520)
(:flew "SL202" 600 725)
(:flew "SL203" 780 910)
(:flew "SL204" 990 1115)
(:flew "SL301" 420 565)
(:flew "SL302" 660 820)
(:flew "SL303" 900 1050)
(:flew "SL304" 1110 1270)
(:flew "SL401" 420 550)
(:flew "SL402" 630 780)
(:flew "SL403" 840 965)
(:flew "SL404" 1035 1173)
(:flew "SL501" 450 615)
(:flew "SL502" 690 835)
(:flew "SL503" 900 1030)
(:flew "SL504" 1110 1260)

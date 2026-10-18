; A disruption for examples/airline.slight --recover on the small day
; (t/256): SL201 leaves 55 minutes late, and the 25 passengers changing
; to SL102 miss it. A quick turn at the hub to try, besides the remedies
; the program makes, and the stand-in services, with a budget of 12.
(:disruption "d" (:delay "SL201" 55))
(:try (:quick-turn "ORD" 570 690 25))
(:check 12)
(:top 3)

; A disruption for examples/airline.slight --recover on the small day
; (t/259): the hub, ORD, closed from 08:00 to 10:00, with the stand-in
; services and a budget of 12. Two of the remedies made come to the same
; day as doing nothing, so the three are checked once.
(:disruption "d" (:close "ORD" 480 600))
(:check 12)
(:top 3)

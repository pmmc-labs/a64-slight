; n-body, after the C version: a body is a vector #(x y z vx vy vz mass),
; changed in place, with Scheme's ordinary arithmetic.
(import (chezscheme))

(define pi 3.141592653589793)
(define solar-mass (* 4.0 pi pi))
(define year 365.24)

(define (planet x y z vx vy vz mass)
    (vector x y z (* vx year) (* vy year) (* vz year) (* mass solar-mass)))

(define bodies
    (list (vector 0.0 0.0 0.0 0.0 0.0 0.0 solar-mass)
          (planet 4.84143144246472090e+00 -1.16032004402742839e+00 -1.03622044471123109e-01
                  1.66007664274403694e-03 7.69901118419740425e-03 -6.90460016972063023e-05
                  9.54791938424326609e-04)
          (planet 8.34336671824457987e+00 4.12479856412430479e+00 -4.03523417114321381e-01
                  -2.76742510726862411e-03 4.99852801234917238e-03 2.30417297573763929e-05
                  2.85885980666130812e-04)
          (planet 1.28943695621391310e+01 -1.51111514016986312e+01 -2.23307578892655734e-01
                  2.96460137564761618e-03 2.37847173959480950e-03 -2.96589568540237556e-05
                  4.36624404335156298e-05)
          (planet 1.53796971148509165e+01 -2.59193146099879641e+01 1.79258772950371181e-01
                  2.68067772490389322e-03 1.62824170038242295e-03 -9.51592254519715870e-05
                  5.15138902046611451e-05)))

(define-syntax field
    (syntax-rules ()
        ((_ b i) (vector-ref b i))))

(define (advance dt)
    (let loop ((bs bodies))
        (unless (null? bs)
            (let ((b (car bs)))
                (for-each
                    (lambda (o)
                        (let* ((dx (- (field b 0) (field o 0)))
                               (dy (- (field b 1) (field o 1)))
                               (dz (- (field b 2) (field o 2)))
                               (dist (sqrt (+ (+ (* dx dx) (* dy dy)) (* dz dz))))
                               (mag (/ dt (* (* dist dist) dist))))
                            (vector-set! b 3 (- (field b 3) (* (* dx (field o 6)) mag)))
                            (vector-set! b 4 (- (field b 4) (* (* dy (field o 6)) mag)))
                            (vector-set! b 5 (- (field b 5) (* (* dz (field o 6)) mag)))
                            (vector-set! o 3 (+ (field o 3) (* (* dx (field b 6)) mag)))
                            (vector-set! o 4 (+ (field o 4) (* (* dy (field b 6)) mag)))
                            (vector-set! o 5 (+ (field o 5) (* (* dz (field b 6)) mag)))))
                    (cdr bs)))
            (loop (cdr bs))))
    (for-each
        (lambda (b)
            (vector-set! b 0 (+ (field b 0) (* dt (field b 3))))
            (vector-set! b 1 (+ (field b 1) (* dt (field b 4))))
            (vector-set! b 2 (+ (field b 2) (* dt (field b 5)))))
        bodies))

(define (energy)
    (let loop ((bs bodies) (e 0.0))
        (if (null? bs)
            e
            (let* ((b (car bs))
                   (e (+ e (* (* 0.5 (field b 6))
                              (+ (+ (* (field b 3) (field b 3)) (* (field b 4) (field b 4)))
                                 (* (field b 5) (field b 5)))))))
                (loop (cdr bs)
                      (fold-left
                          (lambda (e o)
                              (let ((dx (- (field b 0) (field o 0)))
                                    (dy (- (field b 1) (field o 1)))
                                    (dz (- (field b 2) (field o 2))))
                                  (- e (/ (* (field b 6) (field o 6))
                                          (sqrt (+ (+ (* dx dx) (* dy dy)) (* dz dz)))))))
                          e
                          (cdr bs)))))))

(define (offset)
    (let loop ((bs bodies) (px 0.0) (py 0.0) (pz 0.0))
        (if (null? bs)
            (let ((sun (car bodies)))
                (vector-set! sun 3 (/ (- px) solar-mass))
                (vector-set! sun 4 (/ (- py) solar-mass))
                (vector-set! sun 5 (/ (- pz) solar-mass)))
            (let ((b (car bs)))
                (loop (cdr bs)
                      (+ px (* (field b 3) (field b 6)))
                      (+ py (* (field b 4) (field b 6)))
                      (+ pz (* (field b 5) (field b 6))))))))

(define (shown e) (exact (floor (+ (* e 1e9) 0.5))))

(offset)
(display (shown (energy)))
(newline)
(do ((i (string->number (cadr (command-line))) (- i 1))) ((= i 0)) (advance 0.01))
(display (shown (energy)))
(newline)

; spectral-norm, the Benchmarks Game's, single-threaded, a vector a
; vector, with Scheme's ordinary arithmetic.
(import (chezscheme))

(define (a i j)
    (/ 1.0 (+ (quotient (* (+ i j) (+ i j 1)) 2) i 1)))

(define (times-a u v)
    (let ((n (vector-length u)))
        (do ((i 0 (+ i 1))) ((= i n))
            (do ((j 0 (+ j 1)) (s 0.0 (+ s (* (a i j) (vector-ref u j)))))
                ((= j n) (vector-set! v i s))))))

(define (times-at u v)
    (let ((n (vector-length u)))
        (do ((i 0 (+ i 1))) ((= i n))
            (do ((j 0 (+ j 1)) (s 0.0 (+ s (* (a j i) (vector-ref u j)))))
                ((= j n) (vector-set! v i s))))))

(define (times-ata u v w)
    (times-a u w)
    (times-at w v))

(let* ((n (string->number (cadr (command-line))))
       (u (make-vector n 1.0))
       (v (make-vector n 0.0))
       (w (make-vector n 0.0)))
    (do ((i 0 (+ i 1))) ((= i 10))
        (times-ata u v w)
        (times-ata v u w))
    (do ((i 0 (+ i 1))
         (vbv 0.0 (+ vbv (* (vector-ref u i) (vector-ref v i))))
         (vv 0.0 (+ vv (* (vector-ref v i) (vector-ref v i)))))
        ((= i n)
            (display (exact (floor (+ (* (sqrt (/ vbv vv)) 1e9) 0.5))))
            (newline))))

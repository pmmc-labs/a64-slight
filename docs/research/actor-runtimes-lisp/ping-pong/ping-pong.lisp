;; ping-pong: n round trips between two SBCL threads (OS threads), each a
;; message to the other's mailbox (sb-concurrency).
(require :sb-concurrency)
(defpackage :ping-pong (:use :cl :sb-concurrency))
(in-package :ping-pong)

(defun pong (in)
    (loop for m = (receive-message in)
          until (eq (first m) :stop)
          do (send-message (second m) (list :pong (third m)))))

(defun ping (p n)
    (let ((me (make-mailbox)))
        (dotimes (i n)
            (send-message p (list :ping me i))
            (receive-message me))
        (send-message p (list :stop))
        n))

(defun main ()
    (let* ((n (parse-integer (second sb-ext:*posix-argv*)))
           (p (make-mailbox))
           (th (sb-thread:make-thread (lambda () (pong p)))))
        (format t "~d~%" (ping p n))
        (sb-thread:join-thread th)))

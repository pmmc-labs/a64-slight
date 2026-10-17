; Compiles a Chez Scheme program ahead of time, so that its runs don't
; include compiling it: scheme --script bench/chez-compile.ss in.ss out
(compile-program (cadr (command-line)) (caddr (command-line)))

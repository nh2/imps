jit 0 0 0       ; This BGT test is the same as the corresponding BNE test
.fill 16
.fill 28
halt
bgt $0 $0 2     ; should not jump and go to halt, all inside jit
halt            ; should halt here (inside jit)
addi $1 $0 42
halt

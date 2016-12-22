jit 0 0 0       ; This BGT test is the same as the corresponding BEQ test
.fill 16
.fill 32
halt
addi $2 $0 1
bgt $0 $2 2     ; should not jump and go to halt, all inside jit
halt            ; should halt here (inside jit)
addi $1 $0 42
halt

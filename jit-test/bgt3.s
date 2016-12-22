jit 0 0 0       ; This BGT test is the same as the corresponding BNE test
.fill 16
.fill 16
halt            ; should halt here (ouside jit)
bgt $0 $0 2     ; should not jump and go to halt, leaving jit
halt
addi $1 $0 42
halt

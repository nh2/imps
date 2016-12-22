jit 0 0 0       ; This BGE test is the same as the corresponding BEQ test
.fill 16
.fill 20
halt
bge $0 $0 2     ; should jump to addi, leaving jit
halt
addi $1 $0 42
halt            ; should halt here (outside jit)

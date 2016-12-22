jit 0 0 0
.fill 16
.fill 16
halt            ; should halt here (outside jit)
bne $0 $0 2     ; should not jump and go to halt, leaving jit
halt
addi $1 $0 42
halt

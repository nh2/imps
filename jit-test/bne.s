jit 0 0 0
.fill 16
.fill 28
halt
bne $0 $0 2     ; should not jump and go to halt, all inside jit
halt            ; should halt here (inside jit)
addi $1 $0 42
halt

jit 0 0 0
.fill 16
.fill 28
halt            ; should halt here (outside jit)
addi $2 $0 1
bne $0 $2 2     ; should jump and go to addi, all inside jit, then halt outside jit
halt
addi $1 $0 42
halt            ; not here

jit 0 0 0
.fill 16
.fill 24
halt            ; should halt here (outside jit)
jal target      ; should jump to addi
halt
target: addi $1 $0 42
halt

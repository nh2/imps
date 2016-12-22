jit 0 0 0
.fill 16
.fill 20
halt
jal target      ; should jump to addi, leaving jit
halt
target: addi $1 $0 42
halt            ; should halt here (outside jit)

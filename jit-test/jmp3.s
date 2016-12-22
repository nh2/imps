jit 0 0 0       ; Like jmp2.s, just with out the halt instruction JITed as extra check if everything is right
.fill 16
.fill 16
halt
jmp target      ; should jump to addi, leaving jit
halt
target: addi $1 $0 42
halt            ; should halt here (outside jit)

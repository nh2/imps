jit 0 0 0
.fill 16
.fill 28
halt
jmp target      ; should jump to addi, all inside jit
halt
target: addi $1 $0 42
halt            ; should halt here (inside jit)

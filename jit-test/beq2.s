jit 0 0 0
.fill 16
.fill 32
halt
addi $2 $0 1
beq $0 $2 2     ; should not jump and go to halt, all inside jit
halt            ; should halt here (inside jit)
addi $1 $0 42
halt

jit 0 0 0
.fill 16
.fill 20
halt            ; should halt here (outside jit)
addi $2 $0 1
beq $0 $2 2     ; should not jump and go to halt, leaving jit
halt
addi $1 $0 42
halt

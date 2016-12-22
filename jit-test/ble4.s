jit 0 0 0       ; This BLE test is the same as the corresponding BNE test
.fill 16
.fill 20
halt
addi $2 $0 1
ble $0 $2 2     ; should jump and go to addi, leaving jit
halt
addi $1 $0 42
halt            ; should halt here (outside jit)

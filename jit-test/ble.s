jit 0 0 0       ; This BLE test is the same as the corresponding BEQ test
.fill 16
.fill 28
halt
ble $0 $0 2     ; should jump to addi, all inside jit
halt
addi $1 $0 42
halt            ; should halt here (inside jit)

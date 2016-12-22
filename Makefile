all: build

build:
	gcc -Wall -g imps-emulator.c -o imps-emulator

test: build
	./imps-emulator programs/simple.oout > programs/simple.myres
	./imps-emulator programs/matmult.oout > programs/matmult.myres
	./imps-emulator programs/factorial.oout > programs/factorial.myres
	diff programs/simple.res programs/simple.myres
	diff programs/factorial.res programs/factorial.myres
	diff programs/matmult.res programs/matmult.myres

jit: compile_tests
	gcc -z execstack -Wall -g -m32 imps-emulator-jit.c -o imps-emulator-jit

jit_asm:
	gcc -z execstack -Wall -m32 imps-emulator-jit.c -S

jit_test: compile_tests
	gcc -z execstack -Wall -g -m32 imps-emulator-jit.c -o imps-emulator-jit -DDEBUG_ENABLED=0

	./jit-tests.sh

compile_tests:
	./jit-test-compile-s-files.sh

bcat_tests:
	./jit-test-bcat.sh

#!/bin/bash

GREEN='\e[1;32m'
RED='\e[1;31m'
NORMAL='\e[0m'

for t in jit-test/*.oout
do
	base=`basename "$t" .oout`
	echo -n $base
	./imps-emulator-jit "$t" > "jit-test/$base.myres"
	output=`diff -u "jit-test/$base.res" "jit-test/$base.myres"`
	if [ $? -eq 0 ]; then
		echo -e " ${GREEN}OK${NORMAL}"
	else
		echo -e " ${RED}FAILED${NORMAL} with output:"
		echo "$output"
		echo "<<< END OF OUTPUT OF $base >>>"
	fi
done

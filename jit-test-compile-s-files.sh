#!/bin/bash

for sfile in jit-test/*.s
do
	ooutfile=`basename ${sfile} .s`.oout
	echo "$sfile -> $ooutfile"
	./imps-assembler "$sfile" "jit-test/$ooutfile"
done

#!/bin/bash

for ooutfile in jit-test/*.oout
do
	ooutdefile=`basename ${ooutfile} .oout`.oout.de
	echo "$ooutfile -> $ooutdefile"
	./tools/bcat "$ooutfile" > "jit-test/$ooutdefile"
done

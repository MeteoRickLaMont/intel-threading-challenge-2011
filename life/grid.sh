#!/bin/bash
weight=100
jobs=3
for weight in {90..110..2}; do
# for ((weight=90;weight<=110;weight=$weight+2)); do
    # for ((jobs=0;jobs<8;jobs++)); do
	./average.sh ./greedy 09_DoDo_150x150-223533-55.txt 5000 $jobs `echo "$weight*0.1" | bc`
    # done
done

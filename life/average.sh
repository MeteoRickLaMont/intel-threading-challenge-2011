#!/bin/bash
EXEC=${1:-"./greedy"}
INPUT=${2:-"10_DoDo_300x300-373546-15.txt"}
RUNS=${3:-1000}
JOBS=${4:-8}
WEIGHT=${5:-5}

echo "Running $EXEC -j $JOBS -x $WEIGHT $INPUT $RUNS times."
(for ((run=1;run<RUNS;run++)); do $EXEC -j $JOBS -x $WEIGHT $INPUT; done > /dev/null) 2>&1 |
awk -v input=$INPUT -v runs=$RUNS -v jobs=$JOBS -v weight=$WEIGHT \
    '{s+=$3} END {printf "%s,%d,%.1f,%.3f\n", input, jobs, weight, s/runs}'

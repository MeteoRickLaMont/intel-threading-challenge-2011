#!/bin/bash

#
# Default to:
#     executable = "greedy"
#     nruns = 1000
#     nthreads = 8
#     weight = 5
#
EXEC=${1:-"greedy"}
NRUNS=${2:-1000}
NTHREADS=${3:--1}
WEIGHT=${4:--1}

#
# For each of 10 test cases:
#     Do 1 untimed iteration to warm up the cache
#     Do NRUNS timed iterations
#     Print statistics (average, stddev, etc.)
#

# XXX Install TBB and point to build location
# export LD_LIBRARY_PATH=<path/to/wjakob/tbb/build/>
jmfernandez() {
    (./jmfernandez $1) | awk '
        /Time elapsed/ { time = $3 }
        /Solution Path/ { pathlen += length($3) }
        END { printf("%f %s\n", time, pathlen) }
    '
}

nblock() {
    (./nblock $1 output.txt; cat output.txt) 2>&1 | awk '
        /Elapsed time/ { time = $3 }
        /^[0-8]+$/ { pathlen += length($1) }
        END { printf("%f %s\n", time, pathlen) }
    '
}

greedy() {
    (./greedy -j $NTHREADS -w $WEIGHT $1) 2>&1 | awk '
        /Elapsed time/ { time = $3 }
        /^[0-8]+$/ { pathlen += length($1) }
        END { printf("%f %s\n", time, pathlen) }
    '
}

beam() {
    (./beam -j $NTHREADS $1) 2>&1 | awk '
        /Elapsed time/ { time = $3 }
        /^[0-8]+$/ { pathlen += length($1) }
        END { printf("%f %s\n", time, pathlen) }
    '
}

nrun() {
    $EXEC $1 > /dev/null
    for ((i=0;i<$NRUNS;i++)); do
        $EXEC $1
    done | awk '
        BEGIN {
            min = max = 0.0
            minpathlen = maxpathlen = 0
        }
        {
            min = (NR==1 || $1<min ? $1 : min)
            max = (NR==1 || $1>max ? $1 : max)
            sum += $1
            sumsq += $1^2
            minpathlen = (NR==1 || $2<minpathlen ? $2 : minpathlen)
            maxpathlen = (NR==1 || $2>maxpathlen ? $2 : maxpathlen)
            n++
        }
        END {
            if (n>0)
                printf("%10f %10f %10f %10f %6d %6d ", min, max, sum/n, sqrt((sumsq-sum^2/n)/n), minpathlen, maxpathlen);
        }
    '
    echo $1
}

testcases() {
    nrun "01_test-example-1.txt"
    nrun "02_test-example-2.txt"
    nrun "03_test-example-3.txt"
    nrun "04_test-example-4.txt"
    nrun "05_test-example-5.txt"
    nrun "06_test-20x20-glider.txt"
    nrun "07_DoDo_14x14-9385-19.txt"
    nrun "08_DoDo_25x25-63474-39.txt"
    nrun "09_DoDo_150x150-223533-55.txt"
    nrun "10_DoDo_300x300-373546-15.txt"
}

echo "$EXEC algorithm"
if [ "$NTHREADS" != "-1" ]; then
    echo "With $NTHREADS threads."
fi
if [ "$WEIGHT" != "-1" ]; then
    echo "Weight = $WEIGHT"
fi
echo "       min        max        avg     stddev minlen maxlen test"
testcases | awk '
    {
        minsum += $1
        maxsum += $2
        avgsum += $3
        print
    }
    END {
        printf("--------------------------------------------------------------------\n")
        printf("%10f %10f %10f total\n", minsum, maxsum, avgsum)
    }
'

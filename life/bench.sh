#!/bin/sh
echo greedy 1
./greedy 01_test-example-1.txt greedy1.out
echo greedy 2
./greedy 02_test-example-2.txt greedy2.out
echo greedy 3
./greedy 03_test-example-3.txt greedy3.out
echo greedy 4
./greedy 04_test-example-4.txt greedy4.out
echo greedy 5
./greedy 05_test-example-5.txt greedy5.out
echo greedy 6
./greedy 06_test-20x20-glider.txt greedy6.out
echo greedy 7
./greedy 07_DoDo_14x14-9385-19.txt greedy7.out
echo greedy 8
./greedy 08_DoDo_25x25-63474-39.txt greedy8.out
echo greedy 9
./greedy 09_DoDo_150x150-223533-55.txt greedy9.out
echo greedy 10
./greedy 10_DoDo_300x300-373546-15.txt greedy10.out

echo optimal 1
./optimal 01_test-example-1.txt optimal1.out
echo optimal 2
./optimal 02_test-example-2.txt optimal2.out
echo optimal 3
./optimal 03_test-example-3.txt optimal3.out
echo optimal 4
./optimal 04_test-example-4.txt optimal4.out
echo optimal 5
./optimal 05_test-example-5.txt optimal5.out
echo optimal 6
./optimal 06_test-20x20-glider.txt optimal6.out
echo optimal 7
./optimal 07_DoDo_14x14-9385-19.txt optimal7.out
# echo optimal 8
# ./optimal 08_DoDo_25x25-63474-39.txt optimal8.out
# echo optimal 9
# ./optimal 09_DoDo_150x150-223533-55.txt optimal9.out
echo optimal 10
./optimal 10_DoDo_300x300-373546-15.txt optimal0.out

echo jmfernandez 1
./jmfernandez 01_test-example-1.txt
echo jmfernandez 2
./jmfernandez 02_test-example-2.txt
echo jmfernandez 3
./jmfernandez 03_test-example-3.txt
echo jmfernandez 4
./jmfernandez 04_test-example-4.txt
echo jmfernandez 5
./jmfernandez 05_test-example-5.txt
echo jmfernandez 6
./jmfernandez 06_test-20x20-glider.txt
echo jmfernandez 7
./jmfernandez 07_DoDo_14x14-9385-19.txt
echo jmfernandez 8
./jmfernandez 08_DoDo_25x25-63474-39.txt
echo jmfernandez 9
./jmfernandez 09_DoDo_150x150-223533-55.txt
echo jmfernandez 10
./jmfernandez 10_DoDo_300x300-373546-15.txt

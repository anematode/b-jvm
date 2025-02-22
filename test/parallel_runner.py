#!/usr/bin/python
# Credit: https://github.com/doctest/doctest/blob/master/examples/range_based_execution.py

import sys
import math
import multiprocessing
import subprocess
import re

if len(sys.argv) < 2:
    print("supply the path to the doctest executable as the first argument!")
    sys.exit(1)

# get the number of tests in the doctest executable
num_tests = 0

program_with_args = [sys.argv[1], "--count"]
for i in range(2, len(sys.argv)):
    program_with_args.append(sys.argv[i])

result = subprocess.Popen(program_with_args, stdout = subprocess.PIPE).communicate()[0]
result = result.splitlines(True)
for line in result:
    if line.startswith(b"[doctest] unskipped test cases passing the current filters:"):
        num_tests = int(re.search(b"(\\d+)", line).group(1))

# calculate the ranges
cores = multiprocessing.cpu_count()
l = range(num_tests + 1)
n = int(math.ceil(float(len( l )) / cores))
data = [l[i : i + n] for i in range(1, len( l ), n)]
data = tuple([[x[0], x[-1]] for x in data])

# for 8 cores and 100 tests the ranges will look like this
# ([1, 13], [14, 26], [27, 39], [40, 52], [53, 65], [66, 78], [79, 91], [92, 100])

# the worker callback that runs the executable for the given range of tests
def worker(pair):
    first, last = pair
    program_with_args = [sys.argv[1], "--dt-first=" + str(first), "--dt-last=" + str(last)]
    process = subprocess.Popen(program_with_args)
    process.wait()
    return process.returncode


# run the tasks on a pool
if __name__ == '__main__':
    with multiprocessing.Pool(cores) as p:
        processes = [*p.map(worker, data)]
        for i in processes:
            if i != 0:
                print("AT LEAST ONE TEST FAILED")
                sys.exit(1)
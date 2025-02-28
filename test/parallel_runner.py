#!/usr/bin/python3
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

program_with_args = [*sys.argv[1:], "--count"]

result = subprocess.Popen(program_with_args, stdout = subprocess.PIPE).communicate()[0]
result = result.splitlines(True)
for line in result:
    # check if contains
    if b"test cases passing the current filters:" in line:
        num_tests = int(re.findall(b"(\\d+)", line)[-1])

if num_tests == 0:
    print("could not determine the number of tests!")
    sys.exit(1)

# calculate the ranges
cores = multiprocessing.cpu_count()
l = range(num_tests + 1)
n = int(math.ceil(float(len( l )) / cores))
data = [l[i : i + n] for i in range(1, len( l ), n)]
data = tuple([[x[0], x[-1]] for x in data])

# for 8 cores and 100 tests the ranges will look like this
# ([1, 13], [14, 26], [27, 39], [40, 52], [53, 65], [66, 78], [79, 91], [92, 100])

# https://stackoverflow.com/a/4417735/13458117
def execute(cmd):
    popen = subprocess.Popen(cmd, stdout=subprocess.PIPE, universal_newlines=True)
    for stdout_line in iter(popen.stdout.readline, ""):
        yield stdout_line
    popen.stdout.close()
    return_code = popen.wait()

# the worker callback that runs the executable for the given range of tests
def worker(pair):
    first, last = pair
    program_with_args = [*sys.argv[1:], "--dt-first=" + str(first), "--dt-last=" + str(last)]
    zero_failed = False
    for line in execute(program_with_args):
        print(line, end="")
        if "0 failed" in line:
            zero_failed = True
    # Look for "0 failed" in stdout
    return zero_failed

# run the tasks on a pool
if __name__ == '__main__':
    with multiprocessing.Pool(cores) as p:
        processes = [*p.map(worker, data)]
        for j, zero_failed in enumerate(processes):
            if not zero_failed:
                print("AT LEAST ONE TEST FAILED! (process " + str(j) + " had at least one failure)")
                sys.exit(1)
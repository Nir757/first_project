#!/bin/bash

# Create execution times file
touch exec_times.txt

# Create tests directory if it doesn't exist
mkdir -p tests

echo "=== Running Test Cases ==="

# Basic command tests
echo -e "\nTest 1: 6 Arguments Test"
echo "echo arg1 arg2 arg3 arg4 arg5 arg6" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 2: 7 Arguments Test (should fail)"
echo "echo arg1 arg2 arg3 arg4 arg5 arg6 arg7" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 3: Empty Command Test"
echo "" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 4: Space Error Test"
echo "echo  arg1   arg2" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 5: Dangerous Command Test"
echo "rm -rf /" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 6: Simple Pipe Test"
echo "echo hello | grep h" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 7: Pipe with Space Error"
echo "echo  hello | grep h" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 8: Pipe with Dangerous Command"
echo "echo hello | rm -rf /" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 9: Tee Basic Test (1 file)"
echo "echo hello | tee tests/file1.txt" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 10: Tee Test with 2 files"
echo "echo hello | tee tests/file1.txt tests/file2.txt" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 11: Tee Test with 3 files"
echo "echo hello | tee tests/file1.txt tests/file2.txt tests/file3.txt" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 12: Tee Test with -a (append) for 2 files"
echo "echo hello | tee -a tests/file1.txt tests/file2.txt" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 13: Tee Test with -a (append) for 3 files"
echo "echo hello | tee -a tests/file1.txt tests/file2.txt tests/file3.txt" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

echo -e "\nTest 14: Tee Test with Space Error"
echo "echo hello | tee  tests/file1.txt" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

# End the program
echo -e "\nEnding program..."
echo "done" | ./ex1 dangerous_commands.txt exec_times.txt 2>/dev/null

# Cleanup test files
rm -f tests/file*.txt 
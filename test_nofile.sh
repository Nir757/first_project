#!/bin/bash

echo "Current file descriptor limit: $(ulimit -n)"
echo "Trying to open files..."

# Try to open multiple files
for i in {1..10}; do
    if ! exec {i}>"testfile$i.txt" 2>/dev/null; then
        echo "Failed to open file $i"
        break
    fi
    echo "Successfully opened file $i"
done

echo "Done testing" 
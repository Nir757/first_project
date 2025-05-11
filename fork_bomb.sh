#!/bin/bash

# Simple fork bomb script that creates new processes until it hits the limit
echo "Starting process creation test..."
count=0

fork_process() {
    local count=$1
    echo "Process created: $count"
    fork_process $((count+1)) &
    fork_process $((count+1)) &
    sleep 1
}

# Start the fork bomb
fork_process $count 
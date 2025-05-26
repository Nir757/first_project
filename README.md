# Mini Shell Implementation

This project implements a mini shell in C that supports various system programming features including process management, signal handling, and resource limits.

## Project Structure

```
.
├── src/
│   ├── ex1.c              # First version of shell implementation
│   ├── ex2.c              # Second version of shell implementation
│   └── ex3.c              # Current version of shell implementation
├── dangerous_commands.txt # List of dangerous commands to block
├── exec_times.txt        # Execution time logs
├── CMakeLists.txt       # Build configuration
└── README.md           # This file
```

## Features

- Command execution with argument parsing
- Background process support
- Signal handling (SIGCHLD, SIGXCPU, SIGXFSZ, SIGSEGV, SIGUSR1)
- Resource limits management (CPU, memory, file size, open files)
- Dangerous command detection and blocking
- Command execution timing and statistics
- Pipe support
- Matrix calculation with multi-threading
- Custom tee implementation
- Error redirection

## Building the Project

### Using CMake (Recommended)
```bash
mkdir -p build
cd build
cmake ..
make
```

### Using GCC directly
```bash
# For the latest version (ex3)
gcc -o ex3 src/ex3.c -pthread

# For previous versions
gcc -o ex2 src/ex2.c -pthread
gcc -o ex1 src/ex1.c -pthread
```

## Running the Program

After building, run the shell:
```bash
# For the latest version (ex3)
./ex3 dangerous_commands.txt exec_times.txt

# For previous versions
./ex2 dangerous_commands.txt exec_times.txt
./ex1 dangerous_commands.txt exec_times.txt
```

## Usage

The shell supports various commands and features:

1. Basic command execution:
   ```bash
   ls -l
   ps aux
   ```

2. Background processes:
   ```bash
   sleep 10 &
   ```

3. Resource limits:
   ```bash
   rlimit show
   rlimit set cpu=10:20 mem=100M:200M fsize=1G:2G nofile=100:200 ls -l
   ```

4. Matrix calculations:
   ```bash
   mcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "ADD"
   ```

5. Pipe operations:
   ```bash
   ls -l | grep "file"
   ```

6. Error redirection:
   ```bash
   ls nonexistent 2> error.log
   ```

## Error Handling

The shell handles various error conditions:
- Dangerous command detection
- Resource limit violations
- Process execution failures
- Invalid command syntax
- Memory allocation failures
- File operation errors

## Performance Monitoring

The shell tracks and displays:
- Command execution times
- Average execution time
- Minimum and maximum execution times
- Number of blocked dangerous commands 
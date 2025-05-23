# C Project

This project contains C programming exercises and implementations, including system programming, debugging, and performance testing tools.

## Project Structure

```
.
├── src/           # Source files (*.c)
├── docs/          # Documentation
├── tests/         # Test files and scripts
├── build/         # Build artifacts (gitignored)
├── bin/           # Compiled binaries
├── CMakeLists.txt # Build configuration
└── README.md      # This file
```

## Available Programs

- `ex2` - Main program implementation
- `debug` - Debugging utility
- `test_cpu` - CPU testing utility
- `test_limits` - System limits testing
- `memory_intensive_program` - Memory usage testing
- `shell` - Custom shell implementation

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
gcc -o ex2 src/ex2.c
gcc -o debug src/debug.c
gcc -o test_cpu src/test_cpu.c
gcc -o test_limits src/test_limits.c
gcc -o memory_intensive_program src/memory_intensive_program.c
```

## Running the Programs

After building, you can run the executables:
```bash
./ex2 [arguments]
./debug [arguments]
./test_cpu
./test_limits
./memory_intensive_program
```

## Testing

The project includes several test scripts:
- `run_tests.sh` - Main test runner
- `test_nofile.sh` - Tests for file handling
- Various test files in the `tests/` directory

## Development

- Source files are located in the `src/` directory
- Test files and scripts are in the `tests/` directory
- Documentation is in the `docs/` directory
- Compiled binaries are stored in the `bin/` directory 
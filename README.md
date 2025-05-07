# C Project

This project contains C programming exercises and implementations.

## Project Structure

```
.
├── src/           # Source files (*.c)
├── docs/          # Documentation (PDFs)
├── tests/         # Test files and scripts
├── build/         # Build artifacts (gitignored)
├── CMakeLists.txt # Build configuration
└── README.md      # This file
```

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
gcc -o ex1 src/ex1.c
gcc -o ex2 src/ex2.c
gcc -o debug src/debug.c
```

## Running the Programs

After building, you can run the executables:
```bash
./ex1 [arguments]
./ex2 [arguments]
./debug [arguments]
```

## Development

- Source files are located in the `src/` directory
- Test files and scripts are in the `tests/` directory
- Documentation is in the `docs/` directory 
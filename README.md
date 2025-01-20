# DirCat

DirCat is a high-performance C++ utility that concatenates and displays the contents of files in a directory, similar to the Unix `cat` command but for entire directories. It supports multi-threaded processing, recursive directory traversal, and filtering options.

## Features

- Multi-threaded file processing for improved performance
- Recursive directory traversal (optional)
- File extension filtering
- Maximum file size limiting
- Formatted output with file names and syntax highlighting markers
- Graceful interrupt handling
- Memory-efficient streaming of large files

## Requirements

- C++20 compatible compiler
- CMake 3.10 or higher
- Standard C++ libraries

## Usage

```bash
./dircat <directory_path> [options]
```

### Options

- `--max-size <MB>`: Maximum file size in MB (default: 10)
- `--no-recursive`: Disable recursive directory search
- `--ext <ext>`: Process only files with the specified extension

### Examples

Display all files in the current directory:

```bash
./dircat .
```

Process only C++ files up to 20MB in size:

```bash
./dircat . --max-size 20 --ext cpp
```

Process files in the current directory without recursion:

```bash
./dircat . --no-recursive
```

## Output Format

Files are output in the following format:

````
### File: filename.ext
```ext
[file contents]
````

## Implementation Details

- Uses modern C++ features including filesystem, threading, and atomic operations
- Limits maximum thread count to 8 or hardware concurrency, whichever is lower
- Processes files in chunks for memory efficiency
- Maintains consistent output ordering regardless of thread execution order
- Handles large files efficiently through buffered reading

## Error Handling

- Graceful handling of permission denied errors
- Skip files exceeding size limit
- Thread-safe error logging
- Signal handling for clean interruption

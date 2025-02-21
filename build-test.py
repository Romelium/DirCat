#!/usr/bin/env python3

import subprocess, os, sys, argparse, shutil

def get_executable_paths(build_dir, target_name, config):
    """Generates possible executable paths."""
    paths = [os.path.join(build_dir, config, target_name), os.path.join(build_dir, target_name)] # Prioritize config subdir, then build dir root
    common_configs = ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"]
    paths.extend([os.path.join(build_dir, conf, target_name) for conf in common_configs]) # Add common config subdirs
    paths = list(dict.fromkeys(paths)) # Deduplicate, preserve order (Python 3.7+ dict order is insertion)
    if sys.platform == "win32":
        paths = [p + ".exe" if not p.lower().endswith(".exe") else p for p in paths] # Add .exe for Windows
    return paths

def run_cmake_build_and_execute(build_dir = "build", target_name = "dircat_test", generator = None, config = "Debug", clean_build = False):
    """Configures, builds, and executes a CMake target."""
    cmake_cmd = ["cmake", ".", "-G", generator] if generator else ["cmake", "."]
    build_cmd = ["cmake", "--build", build_dir, "--config", config, "--target", target_name]

    if clean_build:
        print(f"Cleaning build directory: {build_dir}")
        shutil.rmtree(build_dir, ignore_errors=True) # More concise cleanup
        print("Build directory cleaned.")
    os.makedirs(build_dir, exist_ok=True)

    try: # Configure
        print(f"Configuring CMake in {build_dir} with '{config}'...")
        subprocess.run(cmake_cmd, cwd=build_dir, check=True, capture_output=True)
        print("CMake configuration successful.")
    except subprocess.CalledProcessError as e:
        print(f"CMake config failed:\n{e.stderr.decode() if e.stderr else e}")
        sys.exit(1)

    try: # Build
        print(f"Building '{target_name}' in {build_dir} with '{config}'...")
        subprocess.run(build_cmd, check=True, capture_output=True)
        print("Build successful.")
    except subprocess.CalledProcessError as e:
        print(f"Build failed:\n{e.stderr.decode() if e.stderr else e}")
        sys.exit(1)

    possible_execs = get_executable_paths(build_dir, target_name, config)
    exec_path = next((p for p in possible_execs if os.path.exists(p)), possible_execs[0] if possible_execs else os.path.join(build_dir, config, target_name)) # Find first existing, fallback

    try: # Execute
        print(f"Executing '{target_name}' from {exec_path}...\n")
        result = subprocess.run([exec_path], check=True, capture_output=True)
        print(f"Execution successful.\nStdout:\n{result.stdout.decode()}")
    except FileNotFoundError:
        print(f"Executable not found: {exec_path}\nSearched paths:\n" + "\n".join(possible_execs)) # Combined message
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Execution failed:\nStdout:n{e.stdout.decode()}\nStderr:\n{e.stderr.decode()}") # Combined stdout/stderr print
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="CMake Build and Execute Script", formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--build-dir", dest="build_dir", default="build", help="Build directory (default: build)")
    parser.add_argument("--target", dest="target_name", default="dircat_test", help="CMake target name (default: dircat_test)")
    parser.add_argument("--generator", dest="generator", help="CMake generator (e.g., Ninja, Makefiles, VS)")
    parser.add_argument("--config", dest="config", default="Debug", help="CMake build config (Debug, Release, etc. Default: Debug)")
    parser.add_argument("--clean", dest="clean_build", action="store_true", help="Clean build directory before building.")

    args = parser.parse_args()
    print("Script arguments:\n" + "\n".join(f"  {k}: {v}" for k, v in vars(args).items())) # Concise arg print
    print("-" * 30)
    run_cmake_build_and_execute(**vars(args)) # Pass args as kwargs

#!/usr/bin/env python3
import os
import sys
import time
import select
import subprocess

def compile_shell():
    print("Compiling posixsh...")
    try:
        subprocess.run(["make", "clean"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        res = subprocess.run(["make"], capture_output=True, text=True)
        if res.returncode != 0:
            print("Compilation failed!")
            print(res.stderr)
            sys.exit(1)
        print("Compilation successful! Executable 'posixsh' ready.")
    except FileNotFoundError:
        print("Error: 'make' or compiler not found. Make sure you are running this inside a Linux environment with build tools.")
        sys.exit(1)

def run_test_case(name, commands, expected_patterns, timeout=1.5):
    print(f"Running Test: {name}...", end="", flush=True)
    
    # Spawn posixsh inside a pseudo-terminal (PTY) to correctly test job control
    pid, fd = os.forkpty()
    if pid == 0:
        # Child process: exec posixsh
        try:
            os.execv("./posixsh", ["./posixsh"])
        except Exception as e:
            sys.exit(127)
            
    # Parent process
    output = b""
    try:
        # Give the shell a moment to print prompt
        time.sleep(0.1)
        
        # Send commands
        for cmd in commands:
            os.write(fd, cmd.encode() + b"\n")
            # Wait briefly between commands to allow execution and terminal update
            time.sleep(0.15)
            
        # Read the terminal output until timeout
        start_time = time.time()
        while time.time() - start_time < timeout:
            r, _, _ = select.select([fd], [], [], 0.1)
            if fd in r:
                try:
                    chunk = os.read(fd, 4096)
                    if not chunk:
                        break
                    output += chunk
                except OSError:
                    break
    finally:
        # Clean up process and descriptor
        try:
            os.close(fd)
            # Terminate child process group
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        except OSError:
            pass

    decoded = output.decode("utf-8", errors="ignore")
    
    # Validate expected patterns in output
    all_passed = True
    failed_pattern = None
    for pattern in expected_patterns:
        if pattern not in decoded:
            all_passed = False
            failed_pattern = pattern
            break
            
    if all_passed:
        print(" [PASSED]")
        return True
    else:
        print(" [FAILED]")
        print(f"  --> Missing pattern: '{failed_pattern}'")
        print("  --> Terminal output received:")
        print("--- START OUTPUT ---")
        print(decoded)
        print("--- END OUTPUT ---")
        return False

def main():
    compile_shell()
    
    # Clean old test files
    if os.path.exists("test_out.txt"):
        os.remove("test_out.txt")

    tests = [
        {
            "name": "Basic Execution (echo)",
            "commands": ["/bin/echo hello_world", "exit"],
            "expected": ["hello_world"]
        },
        {
            "name": "Path Lookup",
            "commands": ["echo search_path_test", "exit"],
            "expected": ["search_path_test"]
        },
        {
            "name": "Builtins (pwd & cd)",
            "commands": ["pwd", "cd /bin", "pwd", "exit"],
            "expected": ["/bin"]
        },
        {
            "name": "Redirection (Output & Input)",
            "commands": [
                "echo redirected_data > test_out.txt",
                "cat < test_out.txt",
                "exit"
            ],
            "expected": ["redirected_data"]
        },
        {
            "name": "Pipes (Single Pipe)",
            "commands": ["echo first_stage | grep first", "exit"],
            "expected": ["first_stage"]
        },
        {
            "name": "Pipes (Multiple Pipes)",
            "commands": ["echo alpha | grep alpha | grep -o a", "exit"],
            "expected": ["a"]
        },
        {
            "name": "Background Jobs",
            "commands": ["sleep 1 &", "jobs", "exit"],
            "expected": ["[1]", "Running"]
        }
    ]

    success_count = 0
    for t in tests:
        if run_test_case(t["name"], t["commands"], t["expected"]):
            success_count += 1
            
    # Cleanup created test files
    if os.path.exists("test_out.txt"):
        os.remove("test_out.txt")

    print(f"\nTest Summary: {success_count}/{len(tests)} tests passed.")
    if success_count == len(tests):
        print("All features verified successfully!")
        sys.exit(0)
    else:
        print("Some tests failed. Check implementation.")
        sys.exit(1)

if __name__ == "__main__":
    main()

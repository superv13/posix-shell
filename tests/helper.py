import os
import sys
import time
import select
import subprocess

def compile_shell():
    """
    Compiles the posixsh shell using make.
    Should be run before running any tests.
    """
    try:
        # Run make from the parent directory of tests/
        root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        subprocess.run(["make", "clean"], cwd=root_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        res = subprocess.run(["make"], cwd=root_dir, capture_output=True, text=True)
        if res.returncode != 0:
            print("Compilation failed!")
            print(res.stderr)
            sys.exit(1)
    except FileNotFoundError:
        print("Error: 'make' or compiler not found. Run tests inside a Linux environment with build tools.")
        sys.exit(1)

def run_shell(commands, timeout=2.0, args=None):
    if args is None:
        args = []    

    """
    Spawns posixsh in a PTY, feeds the commands, and returns the accumulated terminal output.
    """
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    shell_bin = os.path.join(root_dir, "posixsh")
    
    if not os.path.exists(shell_bin):
        compile_shell()

    # Spawn inside a pseudo-terminal (PTY)
    pid, fd = os.forkpty()
    if pid == 0:
        # Child process: exec posixsh
        try:
            os.execv(shell_bin, ["posixsh"] + args)
        except Exception:
            sys.exit(127)
            
    # Parent process
    output = b""
    try:
        time.sleep(0.1)  # Allow shell to print initial prompt
        
        for cmd in commands:
            os.write(fd, cmd.encode() + b"\n")
            time.sleep(0.2)  # Wait for processing
            
        # Read all outputs until timeout
        start_time = time.time()
        while time.time() - start_time < timeout:
            r, _, _ = select.select([fd], [], [], 0.05)
            if fd in r:
                try:
                    chunk = os.read(fd, 4096)
                    if not chunk:
                        break
                    output += chunk
                except OSError:
                    break
    finally:
        try:
            os.close(fd)
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        except OSError:
            pass

    return output.decode("utf-8", errors="ignore")

def assert_contains(name, output, pattern):
    if pattern in output:
        print(f"  [PASS] {name}")
        return True
    else:
        print(f"  [FAIL] {name}")
        print(f"    --> Expected: '{pattern}'")
        print("    --> Actual Output:")
        print(repr(output))
        return False

def assert_not_contains(name, output, pattern):
    """Asserts that `pattern` does NOT appear in `output`."""
    if pattern not in output:
        print(f"  [PASS] {name}")
        return True
    else:
        print(f"  [FAIL] {name}  (unexpected pattern found)")
        print(f"    --> Did NOT want: '{pattern}'")
        print("    --> Actual Output:")
        print(repr(output))
        return False

def cleanup(*paths):
    """Remove temporary files created during a test."""
    for p in paths:
        try:
            os.remove(p)
        except OSError:
            pass

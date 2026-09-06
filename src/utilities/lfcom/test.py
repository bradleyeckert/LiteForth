import subprocess
import time
import sys
import os

# 1. Detect executable target based on host OS structure
EXE_NAME = "./lfcom"
if os.name == "nt" or sys.platform.startswith("win"):
    EXE_NAME = "lfcom.exe" 

# 2. Test Vectors: (Input string, Expected translated output string, Label)
TEST_CASES = [
    # --- Cursor Vectors ---
    (b"\x1b[A", b"\x1b[A", "Cursor Up"),
    (b"\x1b[B", b"\x1b[B", "Cursor Down"),
    
    # --- Function Keys ---
    (b"\x1bOP", b"\x1bOP", "Function Key F1"),
    (b"\x1b[15~", b"\x1b[15~", "Function Key F5"),
    (b"\x1b[24~", b"\x1b[24~", "Function Key F12"),
    
    # --- Mouse Tracking Profiles (SGR 1006) ---
    (b"\x1b[<0;42;15M", b"\x1b[<0;42;15M", "SGR Mouse Left Press"),
    (b"\x1b[<0;42;15m", b"\x1b[<0;42;15m", "SGR Mouse Left Release"),
    (b"\x1b[<64;50;20M", b"\x1b[<64;50;20M", "SGR Mouse Scroll Up"),
    
    # --- Window Controls ---
    (b"\x1b[8;24;80t", b"\x1b[8;24;80t", "Window Resize Event"),
    
    # --- Structural Pass-through Text ---
    (b"Hello Forth 123", b"Hello Forth 123", "Standard Alpha-Numeric Text"),
]

def run_validation():
    print(f"====================================================")
    print(f"          LFCOM UART-TO-STDIO BRIDGE TEST HARNESS   ")
    print(f"====================================================\n")
    print(f"Target Binary: {EXE_NAME}")
    
    if not os.path.exists(EXE_NAME):
        print(f"[-] Error: Compiled binary '{EXE_NAME}' not found in current directory.")
        print(f"    Please move this script to your build output folder.")
        sys.exit(1)
        
    passed_count = 0
    total_count = len(TEST_CASES)
    
    try:
        process = subprocess.Popen(
            [EXE_NAME, "-k"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0 # Force absolute unbuffered raw stream streaming
        )
    except Exception as e:
        print(f"[-] Execution Failure: Could not spawn process. Details: {e}")
        sys.exit(1)

    # Allow internal buffers and threading models a brief startup initialization gap
    time.sleep(0.1)

    for idx, (input_bytes, expected_bytes, desc) in enumerate(TEST_CASES, start=1):
        print(f"[{idx}/{total_count}] Testing: {desc}...")
        
        process.stdin.write(input_bytes)
        process.stdin.flush()
        
        time.sleep(0.02)
        
        output_bytes = b""
        start_poll = time.time()
        
        while (time.time() - start_poll) < 0.1: # Max 100ms reading window
            if os.name == "nt":
                import msvcrt
                import win32pipe
                handle = msvcrt.get_osfhandle(process.stdout.fileno())
                res = win32pipe.PeekNamedPipe(handle, 0)
                avail = res[2]
                if avail > 0:
                    output_bytes += os.read(process.stdout.fileno(), avail)
                    break
            else:
                import select
                r, _, _ = select.select([process.stdout], [], [], 0.01)
                if r:
                    chunk = os.read(process.stdout.fileno(), 1024)
                    output_bytes += chunk
                    if len(output_bytes) >= len(expected_bytes):
                        break
                        
        if output_bytes == expected_bytes:
            print(f"    --> \033[92mPASS\033[0m | Received: {repr(output_bytes)}")
            passed_count += 1
        else:
            print(f"    --> \033[91mFAIL\033[0m | Expected: {repr(expected_bytes)} | Received: {repr(output_bytes)}")

    print(f"\n====================================================")
    print(f"Validation Finished: {passed_count}/{total_count} Tests Passed.")
    print(f"====================================================")
    
    process.terminate()
    process.wait()

if __name__ == "__main__":
    if os.name == "nt":
        os.system("color")
    run_validation()

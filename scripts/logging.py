# Quick logging tool to get debug info being sent over UDP to port
import argparse
import socket
import sys

# Force the terminal output to handle UTF-8 (emojis) and bypass Windows default encoding
# errors='replace' ensures that even if a byte is mangled, the script won't crash
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

parser = argparse.ArgumentParser(description='logging : python -u logging <udp-port>')
parser.add_argument('udpPort', type=int, help='UDP Port')
args = parser.parse_args()

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", args.udpPort))

print(f"--- Listening for UDP logs on port {args.udpPort} (UTF-8 Enabled) ---", flush=True)

oldLog = ""

while True:
    try:
        # Receive packet
        data, addr = sock.recvfrom(2048)

        # Decode the raw bytes from the ESP32
        newLog = data.decode('utf-8', errors='replace').strip()

        # Only print if the message has changed (prevents spam if ESP32 resends)
        if newLog != oldLog:
            # flush=True ensures the data isn't held in a buffer
            print(f"{newLog}", flush=True)
            oldLog = newLog

    except KeyboardInterrupt:
        print("\nLogging stopped by user.", flush=True)
        break
    except Exception as e:
        print(f"\nUnexpected Error: {e}", flush=True)

import sys
import argparse
import esptool
import serial.tools.miniterm
import time
from serial.tools import list_ports

VERSION = "1.2.0"

def find_esp_port():
    """Finds the most likely ESP32 COM port."""
    ports = list_ports.comports()
    targets = ["CP210", "CH340", "USB", "UART", "Silicon Labs"]
    for port in ports:
        if any(target in (port.description or "") for target in targets):
            return port.device
    return None

def main():
    parser = argparse.ArgumentParser(description=f"ESP32 Flasher v{VERSION}")
    parser.add_argument("--chip", required=True, choices=['esp32', 'esp32s3', 'esp32c3'], help="Chip type")
    parser.add_argument("--port", help="COM port (blank for auto-detect)")
    parser.add_argument("--addr", default="0x10000", help="Flash address (default 0x10000)")
    parser.add_argument("imagefile", help="Path to the binary image file")
    parser.add_argument("-s", "--monitor", action="store_true", help="Monitor after flashing")

    args = parser.parse_args()

    # 1. Resolve Port
    target_port = args.port or find_esp_port()
    if not target_port:
        print("--- Error: No ESP32 found. Connect device or specify --port manually. ---")
        sys.exit(1)

    # 2. Flash via esptool
    # Note: Using hyphenated commands for clean output
    flash_args = [
        '--chip', args.chip, '--port', target_port, '--baud', '921600',
        '--before', 'default-reset', '--after', 'hard-reset',
        'write-flash', '-z', args.addr, args.imagefile
    ]

    print(f"\n--- ESP32 Flasher v{VERSION} | Port: {target_port} ---")

    try:
        esptool.main(flash_args)

        # 3. Standard Serial Monitor
        if args.monitor:
            print("\n" + "="*50)
            print(f" MONITORING {target_port} (115200 baud)")
            print(" EXIT: Press  Ctrl + ]")
            print("="*50 + "\n")

            # Brief pause to ensure the port is released by esptool
            time.sleep(0.5)

            # Call the standard miniterm with default Ctrl+] exit
            sys.argv = [sys.argv[0], target_port, '115200']
            serial.tools.miniterm.main()
        else:
            print("\n--- Flash Successful ---")
            input("Press Enter to exit...")

    except Exception as e:
        print(f"\n--- Error: {e} ---")
        input("\nPress Enter to exit...")
        sys.exit(1)

if __name__ == "__main__":
    main()

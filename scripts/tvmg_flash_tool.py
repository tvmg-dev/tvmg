import sys
import argparse
import esptool
import serial.tools.miniterm
import time
import os
from serial.tools import list_ports

VERSION = "1.6.1"

def find_esp_port():
    ports = list_ports.comports()
    targets = ["CP210", "CH340", "USB", "UART", "Silicon Labs"]
    for port in ports:
        if any(target in (port.description or "") for target in targets):
            return port.device
    return None

def get_flash_size(chip_type, port):
    """Temporary call to esptool to detect the actual flash size of the connected chip."""
    try:
        print("--- Detecting Flash Size... ---")
        esp = esptool.cmds.detect_chip(port=port, baud=921600, chip=chip_type)
        esp.connect()
        flash_id = esp.get_flash_id()
        size_bytes = esptool.loader.FLASH_SIZE_NAMES.get(esp.get_flash_size(flash_id), 0)
        esp._port.close()

        if size_bytes == 0:
            print("--- Warning: Could not detect flash size. Defaulting to 4MB. ---")
            return 0x400000

        print(f"--- Detected {size_bytes / (1024*1024):.0f}MB Flash ({hex(size_bytes)}) ---")
        return size_bytes
    except Exception:
        return 0x400000

def main():
    parser = argparse.ArgumentParser(description=f"TVMG Flasher & Backup Tool v{VERSION}")

    parser.add_argument("-c", "--chip", required=True, choices=['esp32', 'esp32s3', 'esp32c3'], help="Chip type")
    parser.add_argument("-p", "--port", help="COM port (blank for auto-detect)")
    parser.add_argument("-a", "--addr", default="0x10000", help="Flash address (default 0x10000)")
    parser.add_argument("-m", "--monitor", action="store_true", help="Monitor after flashing")
    parser.add_argument("-u", "--upload", action="store_true", help="Backup entire flash BEFORE flashing")
    parser.add_argument("-f", "--factory", action="store_true", help="Factory image (address 0x0, size check)")
    parser.add_argument("-o", "--ota", default="0xe000", help="OTA start address (default 0xe000)")
    parser.add_argument("-v", "--version", action="version", version=f"%(prog)s {VERSION}")

    parser.add_argument("imagefile", help="Path to the binary (.bin) file")

    args = parser.parse_args()

    target_port = args.port or find_esp_port()
    if not target_port:
        print("--- Error: No ESP32 found. ---")
        sys.exit(1)

    try:
        # Detect actual flash size for validation and backup
        detected_size = get_flash_size(args.chip, target_port)
        
        # --- FACTORY VALIDATION ---
        target_addr = args.addr
        if args.factory:
            target_addr = "0x0"
            file_size = os.path.getsize(args.imagefile)
            if file_size != detected_size:
                print(f"--- Error: Factory image size ({file_size}) does not match flash size ({detected_size}) ---")
                sys.exit(1)

        # --- PHASE 1: UPLOAD (BACKUP) ---
        if args.upload:
            backup_file = "original_image.bin"
            print(f"\n--- Reading entire {hex(detected_size)} flash to {backup_file} ---")
            backup_args = [
                '--chip', args.chip, '--port', target_port, '--baud', '921600',
                'read-flash', '0', hex(detected_size), backup_file
            ]
            esptool.main(backup_args)
            print("--- Backup Complete ---")
            time.sleep(1)

        # --- PHASE 2: OTA RESET (APP ONLY) ---
        if not args.factory:
            print(f"--- Resetting OTA Selection at {args.ota} ---")
            # Changed 'erase_region' to 'erase-region' to fix deprecation warning
            ota_args = [
                '--chip', args.chip, '--port', target_port, '--baud', '921600',
                'erase-region', args.ota, '0x2000'
            ]
            esptool.main(ota_args)

        # --- PHASE 3: FLASH (DOWNLOAD) ---
        print(f"\n--- Flashing {args.imagefile} to {args.chip} at {target_addr} ---")
        flash_args = [
            '--chip', args.chip, '--port', target_port, '--baud', '921600',
            '--before', 'default-reset', '--after', 'hard-reset',
            'write-flash', '-z', target_addr, args.imagefile
        ]
        esptool.main(flash_args)

        # --- PHASE 4: MONITOR ---
        if args.monitor:
            print("\n" + "="*50)
            print(f" MONITORING {target_port} | EXIT: Ctrl + ]")
            print("="*50 + "\n")
            time.sleep(0.3)
            sys.argv = [sys.argv[0], target_port, '115200']
            serial.tools.miniterm.main()
        else:
            print("\n--- All Operations Successful ---\n\n")

    except Exception as e:
        print(f"\n--- Error: {e} ---")
        input("\nPress Enter to exit...")
        sys.exit(1)

if __name__ == "__main__":
    main()
import serial.tools.list_ports

def find_esp32s3_board():
    # Vendor ID for Espressif
    ESP_VID = 0x303A

    # Common PIDs for S3:
    # 0x8231 is from your boards.local.txt
    # 0x4001 is the ESP32-S3 ROM Bootloader mode
    # 0x1001 is the standard S3 USB CDC
    TARGET_PIDS = [0x8231, 0x4001, 0x1001]

    ports = serial.tools.list_ports.comports()
    detected_ports = []

    for port in ports:
        # Using .get() style or direct access with safety
        vid = port.vid if port.vid is not None else 0
        pid = port.pid if port.pid is not None else 0

        print(f"Checking: {port.device} [VID:{hex(vid)}, PID:{hex(pid)}]")

        if vid == ESP_VID and pid in TARGET_PIDS:
            detected_ports.append(port.device)

    return detected_ports

if __name__ == "__main__":
    try:
        boards = find_esp32s3_board()
        if boards:
            # Replaced emoji with [OK] to avoid UnicodeEncodeError
            print(f"\n[OK] Found Waveshare LCD at: {boards[0]}")
        else:
            print("\n[!] No compatible board found. Check your USB cable!")
    except Exception as e:
        print(f"\n[ERROR] An error occurred: {e}")

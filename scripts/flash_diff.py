import sys
import os
import csv
import argparse

VERSION = "1.6.0"

def parse_partitions(csv_path):
    """Parses a standard ESP-IDF partition CSV file."""
    partitions = []
    try:
        with open(csv_path, 'r') as f:
            lines = [line for line in f if line.strip() and not line.strip().startswith('#')]
            reader = csv.reader(lines, skipinitialspace=True)

            first_offset = 0xFFFFFF # High sentinel

            for row in reader:
                if len(row) < 5: continue
                name = row[0].strip()
                offset = int(row[3].strip(), 16)
                size = int(row[4].strip(), 16)

                if offset < first_offset:
                    first_offset = offset

                if name.lower() in ['nvs', 'otadata']:
                    continue

                partitions.append((name, offset, size))

        # Determine Bootloader range:
        # On original ESP32, first_offset is usually 0x8000 (partitions start after bootloader/app)
        # We check the area from 0x0 up to the start of the first defined partition/NVS.
        # This covers 0x0-0x8000 or 0x1000-0x8000 automatically.
        partitions.insert(0, ("Bootloader/PT", 0x0, first_offset))

    except Exception as e:
        print(f"Error parsing CSV: {e}")
        sys.exit(1)

    return partitions

def compare_segments(file1, file2, segments):
    if not os.path.exists(file1) or not os.path.exists(file2):
        print(f"Error: Missing files.")
        return

    print(f"\nComparing: {os.path.basename(file1)} vs {os.path.basename(file2)}")
    print(f"{'Partition':<15} | {'Offset':<10} | {'Size':<10} | {'Status'}")
    print("-" * 70)

    with open(file1, 'rb') as f1, open(file2, 'rb') as f2:
        for name, offset, size in segments:
            f1.seek(offset)
            f2.seek(offset)

            chunk1 = f1.read(size)
            chunk2 = f2.read(size)

            if chunk1 == chunk2:
                status = "✅ MATCH"
            else:
                diff_idx = next((i for i in range(len(chunk1)) if chunk1[i] != chunk2[i]), None)
                # Correct the reported address based on file offset
                status = f"❌ DIFF @ {hex(offset + diff_idx)}"

            print(f"{name:<15} | {hex(offset):<10} | {hex(size):<10} | {status}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=f"Smart Partition Compare v{VERSION}")
    parser.add_argument("master", help="Master .bin file")
    parser.add_argument("device", help="Read-back .bin file")
    parser.add_argument("-p", "--partitions", required=True, help="Path to partitions.csv")

    args = parser.parse_args()

    parts = parse_partitions(args.partitions)
    compare_segments(args.master, args.device, parts)

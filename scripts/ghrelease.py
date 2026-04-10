import subprocess
import json
import os
import sys
import argparse
import hashlib

# --- Configuration ---
REPO = "tvmg-dev/otatest"
FIXED_TAG = "binaries"
MANIFEST_NAME = "otamanifest.json"

# Hardware ID in manifest to binary filename map
HW_MAP = {
    "TVMG-ESP32": "tvmg_esp32",
    "TVMG-ESP32S3-RS485": "tvmg_esp32s3",
    "WS-ESP32-S3-Touch-LCD-4.3B": "tvmg_wsharelcd",
    "WS-ESP32-S3-Relay-1CH": "tvmg_wsrelay"
}

def run_gh(args):
    """Executes GitHub CLI commands and returns output."""
    cmd = ["gh", "-R", REPO] + args
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result

def get_binaries_in_release(tag):
    """Returns a list of filenames currently in the GitHub release."""
    res = run_gh(["release", "view", tag, "--json", "assets"])
    if res.returncode != 0: return []
    data = json.loads(res.stdout)
    return [asset['name'] for asset in data.get('assets', [])]

def get_md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()

def handle_upload(args):
    """Step 1: Upload binaries to the fixed release area."""
    print(f"[*] Checking existence of binaries for version {args.version}...")
    existing_files = get_binaries_in_release(FIXED_TAG)
    
    devices_to_process = HW_MAP.keys() if args.device == 'all' else [args.device]
    
    for hw in devices_to_process:
        prefix = HW_MAP[hw]
        # Expecting format: prefix-version.bin
        filename = f"{args.version}-{prefix}.bin"
        local_path = os.path.join(args.dir, filename)

        if filename in existing_files:
            print(f"[!] Error: {filename} already exists in GitHub release. Upload aborted.")
            sys.exit(1)

        if not os.path.exists(local_path):
            print(f"[!] Error: Local file {local_path} not found.")
            sys.exit(1)

        print(f"[+] Uploading {filename}...")
        res = run_gh(["release", "upload", FIXED_TAG, local_path])
        if res.returncode != 0:
            print(f"[!] Failed to upload {filename}: {res.stderr}")
            sys.exit(1)

def handle_publish(args):
    """Step 2: Update and publish the manifest."""
    print(f"[*] Fetching {MANIFEST_NAME}...")
    res = run_gh(["release", "download", FIXED_TAG, "-p", MANIFEST_NAME, "--clobber"])
    if res.returncode != 0:
        print("[!] Could not download manifest.")
        sys.exit(1)

    with open(MANIFEST_NAME, "r") as f:
        manifest = json.load(f)

    existing_files = get_binaries_in_release(FIXED_TAG)
    devices_to_process = HW_MAP.keys() if args.device == 'all' else [args.device]

    for hw in devices_to_process:
        # Check if device exists in manifest
        if hw not in manifest:
            print(f"[!] Error: Device {hw} not found in manifest.")
            sys.exit(1)
        
        # Check if group exists for that device
        if args.group not in manifest[hw]:
            print(f"[!] Error: Group {args.group} not found for {hw}.")
            sys.exit(1)

        # Check if binary exists in GitHub
        filename = f"{HW_MAP[hw]}-{args.version}.bin"
        if filename not in existing_files:
            print(f"[!] Error: Binary {filename} not found in GitHub. Run 'upload' first.")
            sys.exit(1)

        # Update Manifest Data
        print(f"[*] Updating {hw} [{args.group}] to {args.version}...")
        run_gh(["release", "download", FIXED_TAG, "-p", filename, "--clobber"])
        
        manifest[hw][args.group] = {
            "v": args.version,
            "md5": get_md5(filename),
            "url": f"https://github.com/{REPO}/releases/download/{FIXED_TAG}/{filename}"
        }
        os.remove(filename)

    # Finalize
    with open(MANIFEST_NAME, "w") as f:
        json.dump(manifest, f, indent=2)
    
    print("[+] Uploading updated manifest...")
    run_gh(["release", "upload", FIXED_TAG, MANIFEST_NAME, "--clobber"])
    print("[***] Publish Complete.")

# --- CLI Setup ---
parser = argparse.ArgumentParser(description="TVMG OTA Release Manager")
subparsers = parser.add_subparsers(dest="command", required=True)

# Upload Command
up_parser = subparsers.add_parser("upload")
up_parser.add_argument("version", help="Version string (e.g., v26.04.01b)")
up_parser.add_argument("--device", default="all", choices=list(HW_MAP.keys()) + ["all"])
up_parser.add_argument("--dir", default="./builds", help="Local directory containing .bin files")

# Publish Command
pub_parser = subparsers.add_parser("publish")
pub_parser.add_argument("group", choices=["alpha", "beta", "release"])
pub_parser.add_argument("version", help="Version string to set in manifest")
pub_parser.add_argument("--device", default="all", choices=list(HW_MAP.keys()) + ["all"])

if __name__ == "__main__":
    args = parser.parse_args()
    if args.command == "upload":
        handle_upload(args)
    elif args.command == "publish":
        handle_publish(args)
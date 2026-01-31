#!/bin/bash

# Define absolute tool paths within the Docker container
readonly ESPTOOL_BIN="/root/.arduino15/packages/esp32/tools/esptool_py/4.9.dev3/esptool"
readonly GEN_PART_PY="/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/tools/gen_esp32part.py"
readonly OTADATA_BIN="/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/tools/partitions/boot_app0.bin"

function generate_factory_image() {
    local variant=$1
    local out_dir="build/output/${variant}"
    local fs_src_root="filesystems/${variant}"

    # Identify sketch from current working directory
    local sketch_name=$(basename "$(pwd)")
    local part_bin="${out_dir}/${sketch_name}.ino.partitions.bin"
    local boot_bin="${out_dir}/${sketch_name}.ino.bootloader.bin"
    local app_bin="${out_dir}/${sketch_name}.ino.bin"

    # 1. Validation
    if [[ ! -f "$part_bin" || ! -f "$boot_bin" || ! -f "$app_bin" ]]; then
        echo "Error: Missing build artifacts in $out_dir"
        echo "Ensure you have compiled for $variant before running -factory"
        return 1
    fi

    echo "-------------------------------------------------------"
    echo "FACTORY IMAGE GENERATION: $variant"
    echo "SOURCE: $sketch_name"
    echo "-------------------------------------------------------"

    # 2. Extract Metadata (The Source of Truth)
    # Use -q to suppress headers and get clean CSV output
    local part_data=$(python3 "$GEN_PART_PY" -q "$part_bin")

    if [[ -z "$part_data" ]]; then
        echo "Error: Failed to parse partition table."
        return 1
    fi

    # Calculate Max Flash Extent for --flash_size
    # We strip spaces from the raw data to ensure awk math works
    local max_byte=$(echo "$part_data" | sed 's/[[:space:]]//g' | grep -v "#" | awk -F, '{printf "%d\n", $4+$5}' | sort -n | tail -1)

    local flash_size="4MB"
    [[ $max_byte -gt 4194304 ]] && flash_size="8MB"
    [[ $max_byte -gt 8388608 ]] && flash_size="16MB"
    [[ $max_byte -gt 16777216 ]] && flash_size="32MB"

    # 3. Detect Chip Type
    local chip_type=$($ESPTOOL_BIN image_info "$boot_bin" | grep "Detected image type" | cut -d':' -f2 | xargs | tr '[:upper:]' '[:lower:]' | sed 's/-//g')

    echo "Detected Chip: $chip_type | Required Flash: $flash_size"

    # 4. Sync App to Recovery (If directory exists)
    if [[ -d "$fs_src_root/recovery" ]]; then
        echo "Syncing app binary to recovery folder..."
        cp "$app_bin" "$fs_src_root/recovery/factory_reset.bin"
    fi

    # 5. Initialize Merge Command as an ARRAY
    local merge_args=(
        "--chip" "$chip_type"
        "merge_bin"
        "-o" "${out_dir}/factory_complete.bin"
        "--flash_mode" "dio"
        "--flash_size" "$flash_size"
        "0x0000" "$boot_bin"
        "0x8000" "$part_bin"
        "0xe000" "$OTADATA_BIN"
        "0x10000" "$app_bin"
    )

    # 6. Process Filesystems
    if [[ -d "$fs_src_root" ]]; then
        # Iterate through subdirectories in the filesystem source root
        for fs_folder_path in "${fs_src_root}"/*/; do
            # Skip if no directories found
            [[ -d "$fs_folder_path" ]] || continue

            local fs_folder=$(basename "$fs_folder_path")
            echo "Checking filesystem folder: $fs_folder"

            # Match partition name even if there are spaces in the CSV output
            local line=$(echo "$part_data" | grep -E "^${fs_folder}[[:space:]]*," | head -n 1)

            if [[ -n "$line" ]]; then
                # Clean the line of all spaces so 'cut' fields are reliable
                local clean_line=$(echo "$line" | sed 's/[[:space:]]//g')

                local subtype=$(echo "$clean_line" | cut -d',' -f3)
                local offset_raw=$(echo "$clean_line" | cut -d',' -f4)
                local size_raw=$(echo "$clean_line" | cut -d',' -f5)
                local img_bin="${out_dir}/${fs_folder}.bin"

                # Convert Size to Bytes for the tools
                local size_bytes=$size_raw
                if [[ "$size_raw" == *K ]]; then
                    size_bytes=$(( ${size_raw%K} * 1024 ))
                elif [[ "$size_raw" == *M ]]; then
                    size_bytes=$(( ${size_raw%M} * 1024 * 1024 ))
                fi

                echo "Packaging $fs_folder ($subtype) | Size: $size_bytes bytes | Offset: $offset_raw"

                if [[ "$subtype" == "spiffs" ]]; then
                    mkspiffs -c "${fs_src_root}/${fs_folder}" -s "$size_bytes" "$img_bin"
                else
                    # littlefs packaging - FIXED to 4096 block size
                    mklittlefs -c "${fs_src_root}/${fs_folder}" -s "$size_bytes" -p 256 -b 4096 "$img_bin"
                fi

                # Add to merge array if binary was created
                if [[ -f "$img_bin" ]]; then
                    merge_args+=("$offset_raw" "$img_bin")
                else
                    echo "ERROR: Failed to create image for $fs_folder"
                    return 1
                fi
            else
                echo "Warning: No partition found matching folder name '$fs_folder' - skipping."
            fi
        done
    fi

    # 7. Final Merge Execution
    echo "-------------------------------------------------------"
    echo "Merging All Binaries into Factory Image..."

    # Run esptool with the constructed array
    $ESPTOOL_BIN "${merge_args[@]}"

    if [[ $? -eq 0 ]]; then
        echo "SUCCESS: factory_complete.bin created in $out_dir"
    else
        echo "ERROR: esptool merge_bin failed."
        return 1
    fi
}

# Execute
generate_factory_image "$1"

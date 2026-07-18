#!/bin/bash

# Define absolute tool paths within the Docker container
readonly ESPTOOL_BIN="/opt/arduino/packages/esp32/tools/esptool_py/4.9.dev3/esptool"
readonly GEN_PART_PY="/opt/arduino/packages/esp32/hardware/esp32/3.1.0/tools/gen_esp32part.py"
readonly OTADATA_BIN="/opt/arduino/packages/esp32/hardware/esp32/3.1.0/tools/partitions/boot_app0.bin"

function generate_factory_image() {
    local board_name=$1
    local out_dir="build/${board_name}/output"
    local fs_src_root="filesystems/${board_name}"

    printf "\nFactory Image Generation for: $board_name\n\n"

    # Validate filesystem folder exists and is not empty
    if [[ ! -d "$fs_src_root" ]]; then
        printf "Error: Filesystem folder not found: $fs_src_root\n" >&2
        return 1
    fi

    if [[ -z "$(ls -A "$fs_src_root")" ]]; then
        printf "Error: Filesystem folder is empty: $fs_src_root\n" >&2
        return 1
    fi

    # Identify sketch from current working directory
    local sketch_name=$(basename "$(pwd)")
    local part_bin="${out_dir}/${sketch_name}.ino.partitions.bin"
    local boot_bin="${out_dir}/${sketch_name}.ino.bootloader.bin"
    local app_bin="${out_dir}/${board_name}.bin"
    
    # Check for artefacts
    if [[ ! -f "$part_bin" || ! -f "$boot_bin" || ! -f "$app_bin" ]]; then
        printf "Error: Missing build artifacts in $out_dir"
        return 1
    fi

    # Extract Bootloader Header Metadata (the source of 'truth')
    local header_hex=$(od -An -N4 -t x1 "$boot_bin" | tr -d ' ')

    local magic=${header_hex:0:2}
    if [[ "$magic" != "e9" ]]; then
        printf "Error: $boot_bin is not a valid ESP image header."
        return 1
    fi

    # Decode Size and Freq (Byte 3)
    local byte3_hex=${header_hex:6:2}
    local byte3_dec=$((16#$byte3_hex))
    local size_nibble=$(( (byte3_dec >> 4) & 0xF ))

    case "$size_nibble" in
        0) b_size="1MB" ;;
        1) b_size="2MB" ;;
        2) b_size="4MB" ;;
        3) b_size="8MB" ;;
        4) b_size="16MB" ;;
        5) b_size="32MB" ;;
        *) b_size="keep" ;;
    esac

    # Detect Chip Type and Determine Bootloader Offset
    local chip_type=$($ESPTOOL_BIN image_info "$boot_bin" | grep "Detected image type" | cut -d':' -f2 | xargs | tr '[:upper:]' '[:lower:]' | sed 's/-//g')

    # EXPLICIT logic: Standard legacy ESP32 MUST use 0x1000.
    # later cpu's, S and C series (S2, S3, C3, etc.) use 0x0000.
    local boot_offset="0x1000"
    if [[ "$chip_type" == "esp32s2" || "$chip_type" == "esp32s3" || "$chip_type" == "esp32c3" || "$chip_type" == "esp32c6" ]]; then
        boot_offset="0x0000"
    fi

    printf "Detected Chip: $chip_type | Bootloader Offset: $boot_offset | Flash Size: $b_size\n"

    # Extract Partition Metadata for App and OTA Offsets from the partition binary
    local part_data=$(python3 "$GEN_PART_PY" -q "$part_bin")
    local app_offset=$(echo "$part_data" | grep -E "app|factory" | head -n 1 | cut -d',' -f4 | xargs)
    local ota_offset=$(echo "$part_data" | grep "otadata" | head -n 1 | cut -d',' -f4 | xargs)

    # Write the app to a recovery partition (If directory exists)
    if [[ -d "$fs_src_root/recovery" ]]; then
        cp "$app_bin" "$fs_src_root/recovery/factory_app.bin"
    fi

    # Final merge command
    local merge_args=(
        "--chip" "$chip_type"
        "merge_bin"
        "-o" "${out_dir}/${board_name}_factory.bin"
        "--fill-flash-size" "$b_size"
        "$boot_offset" "$boot_bin"
        "0x8000" "$part_bin"
    )

    [[ -n "$ota_offset" ]] && merge_args+=("$ota_offset" "$OTADATA_BIN")
    merge_args+=("$app_offset" "$app_bin")

    # Generate the filesystems - there may be multiple partitions
    if [[ -d "$fs_src_root" ]]; then
        for fs_folder_path in "${fs_src_root}"/*/; do
            [[ -d "$fs_folder_path" ]] || continue
            local fs_folder=$(basename "$fs_folder_path")
            local line=$(echo "$part_data" | grep -E "^${fs_folder}[[:space:]]*," | head -n 1)

            if [[ -n "$line" ]]; then
                local clean_line=$(echo "$line" | sed 's/[[:space:]]//g')
                local subtype=$(echo "$clean_line" | cut -d',' -f3)
                local offset_raw=$(echo "$clean_line" | cut -d',' -f4)
                local size_raw=$(echo "$clean_line" | cut -d',' -f5)
                local img_bin="${out_dir}/${fs_folder}.bin"

                local size_bytes=$size_raw
                [[ "$size_raw" == *K ]] && size_bytes=$(( ${size_raw%K} * 1024 ))
                [[ "$size_raw" == *M ]] && size_bytes=$(( ${size_raw%M} * 1024 * 1024 ))

                if [[ "$subtype" == "spiffs" ]]; then
                    mkspiffs -c "${fs_src_root}/${fs_folder}" -s "$size_bytes" "$img_bin"
                else
                    mklittlefs -c "${fs_src_root}/${fs_folder}" -s "$size_bytes" -p 256 -b 4096 "$img_bin"
                fi
                [[ -f "$img_bin" ]] && merge_args+=("$offset_raw" "$img_bin")
            fi
        done
    fi

    # Finally perform the merge operation
    printf "Merging into ${board_name}_complete.bin ($b_size)...\n"
    printf "Arguments: %s\n" "${merge_args[*]}"
    
    $ESPTOOL_BIN "${merge_args[@]}"
}

generate_factory_image "$1"

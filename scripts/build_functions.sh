function espbuild()
{
  # 1. Parameter Handling
  local board_name=""
  local factory_mode=false
  local verbose_flag="--verbose"

  for arg in "$@"; do
    if [[ "$arg" == "-factory" ]]; then
      factory_mode=true
    else
      board_name="$arg"
    fi
  done

  # Default board if none provided
  board_name="${board_name:-tvmg_wsharelcd}"
  local internal_fqbn="esp32:esp32:${board_name}"
  local sketch_dir=$(basename "$(pwd)")

  # 2. Path Preparation
  local local_cache_path="build/cache/${board_name}"
  local local_output_path="build/output/${board_name}"
  mkdir -p "$local_cache_path" "$local_output_path"

  # 3. Docker Command Base (Restored to your working string format)
  local DOCKER_BASE="MSYS_NO_PATHCONV=1 docker run --rm \
    -v \"/$(pwd):/root/Arduino/$sketch_dir\" \
    -v \"/$(pwd)/$local_cache_path:/root/build_core\" \
    -v \"/c/Users/$(whoami)/Documents/Arduino/libraries:/shared_libs\" \
    -v \"/$(pwd)/arduino/boards.local.txt:/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/boards.local.txt\" \
    -v \"/$(pwd)/arduino/tvmg_littlefs_16MB.csv:/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/tools/partitions/tvmg_littlefs_16MB.csv\" \
    -v \"/$(pwd)/arduino/tvmg_ota_512KB_spiffs_4MB.csv:/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/tools/partitions/tvmg_ota_512KB_spiffs_4MB.csv\" \
    -w \"/root/Arduino/$sketch_dir\" \
    tvmg-builder"

  # --- PATH A: FACTORY IMAGE ---
  if [[ "$factory_mode" == true ]]; then
    echo ">>> Executing REPRODUCIBLE Factory Merge: $board_name"
    eval "$DOCKER_BASE /bin/bash ./scripts/gen_factory_image.sh $board_name"
    return $?
  fi

  # --- PATH B: ARDUINO-CLI BUILD ---
  echo "-------------------------------------------------------"
  echo "SKETCH: $sketch_dir | BOARD: $board_name"
  echo "-------------------------------------------------------"

  local cmd="mkdir -p /root/build_core/work; \
             rm -f /root/build_core/work/partitions.csv; \
             arduino-cli compile --jobs 8 $verbose_flag \
             --build-path /root/build_core/work \
             --libraries /shared_libs \
             --fqbn $internal_fqbn \
             --output-dir /root/Arduino/$sketch_dir/$local_output_path ."

  eval "$DOCKER_BASE /bin/bash -c '$cmd'"
  local build_status=$?

  # 4. Post-Build: Copy and Rename Binary
  if [ $build_status -eq 0 ]; then
    local source_bin="${local_output_path}/${sketch_dir}.ino.bin"
    local target_bin="${local_output_path}/${board_name}.bin"

    if [ -f "$source_bin" ]; then
      cp "$source_bin" "$target_bin"
      echo " Success: Copied $source_bin to $target_bin"
    else
      echo " Warning: Could not find $source_bin to rename."
    fi
  else
    echo " Build failed with exit code $build_status"
  fi

  return $build_status
}

alias espbuild-32='espbuild tvmg_esp32'
alias espbuild-s3='espbuild tvmg_esp32s3'
alias espbuild-ws='espbuild tvmg_wsharelcd'

function flash-32()
{
  local file="./build/output/tvmg_esp32/tvmg_esp32.bin"
  local address="0x10000"
  local factory_mode=false
  local port="COM3"

  # Check for -factory flag
  for arg in "$@"; do
    if [[ "$arg" == "-factory" ]]; then
      factory_mode=true
    fi
  done

  if [[ "$factory_mode" == true ]]; then
    file="./build/output/tvmg_esp32/factory_complete.bin"
    address="0x0"
    echo ">>> Factory Mode: Flashing complete image to $address"
  fi

  if [ ! -f "$file" ]; then
    printf " Not found: $file\n\n"
  else
    # The flash_mode, flash_size and flash_freq are in the bootloader header
    # and the 'keep' option for those settings is the default
    #
    # When we program a full image then the bootloader will have the modes
    # set.  Programming an image only doesn't need such headers

    python -m esptool --chip esp32 --port $port --baud 921600 \
    --before default_reset --after hard_reset write_flash -z \
    "$address" "$file"

    if [ $? ]; then
      echo "  Launching Monitor..."
      winpty python -m esp_idf_monitor --port $port --baud 115200
    fi
  fi
}

function flash()
{
   local port=$1
   local variant=$2


}

function flash-ws()
{
  local file="./build/output/tvmg_wsharelcd/tvmg_wsharelcd.bin"
  local address="0x10000"
  local factory_mode=false
  local port="COM8"

  # Check for -factory flag
  for arg in "$@"; do
    if [[ "$arg" == "-factory" ]]; then
      factory_mode=true
    fi
  done

  if [[ "$factory_mode" == true ]]; then
    file="./build/output/tvmg_wsharelcd/factory_complete.bin"
    address="0x0"
    echo ">>> Factory Mode: Flashing complete image to $address"
  fi

  if [ ! -f "$file" ]; then
    printf " Not found: $file\n\n"
  else
    # The flash_mode, flash_size and flash_freq are in the bootloader header
    # and the 'keep' option for those settings is the default
    #
    # When we program a full image then the bootloader will have the modes
    # set.  Programming an image only doesn't need such headers

    python -m esptool --chip esp32s3 --port $port --baud 921600 \
    --before default_reset --after hard_reset write_flash -z \
    "$address" "$file"

    if [ $? ]; then
      echo "  Launching Monitor..."
      winpty python -m esp_idf_monitor --port $port --baud 115200
    fi
  fi
}

function flash-s3()
{
  local file="./build/output/tvmg_esp32s3/tvmg_esp32s3.bin"
  local address="0x10000"
  local factory_mode=false
  local port="COM6"

  # Check for -factory flag
  for arg in "$@"; do
    if [[ "$arg" == "-factory" ]]; then
      factory_mode=true
    fi
  done

  if [[ "$factory_mode" == true ]]; then
    file="./build/output/tvmg_esp32s3/factory_complete.bin"
    address="0x0"
    echo ">>> Factory Mode: Flashing complete image to $address"
  fi

  if [ ! -f "$file" ]; then
    printf " Not found: $file\n\n"
  else
    # The flash_mode, flash_size and flash_freq are in the bootloader header
    # and the 'keep' option for those settings is the default
    #
    # When we program a full image then the bootloader will have the modes
    # set.  Programming an image only doesn't need such headers

    python -m esptool --chip esp32s3 --port $port --baud 921600 \
    --before default_reset --after hard_reset write_flash -z \
    "$address" "$file"

    if [ $? ]; then
      echo "  Launching Monitor..."
      winpty python -m esp_idf_monitor --port $port --baud 115200
    fi
  fi
}

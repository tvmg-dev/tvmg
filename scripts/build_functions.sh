function espbuild()
{
  # 1. Parameter Handling
  local board_name=""
  local factory_mode=true
  local verbose_flag="--verbose"
  local docker_user=""
  local jobs_num=4

  local win_docs_raw=$(powershell.exe -Command "[Environment]::GetFolderPath('MyDocuments')" 2>/dev/null | tr -d '\r')
  local win_docs=$(wslpath "$win_docs_raw")

  local library_host_path="${win_docs}/Arduino/libraries"

  if [[ "$OSTYPE" == "linux-gnu"* ]]; then
      echo "Linux environment"
      library_host_path="$(pwd)/libraries"
      docker_user="-u $(id -u):$(id -g)"

  fi

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

  # Output paths - for the build cache and final artefacts
  local local_cache_path="build/cache/${board_name}"
  local local_output_path="build/output/${board_name}"
  rm -rf "${local_output_path}"
  mkdir -p "$local_cache_path" "$local_output_path"

  # Docker - base command
  #   Probably need a better way of putting the partitions in place ?
  local DOCKER_BASE="MSYS_NO_PATHCONV=1 docker run ${docker_user} --rm \
    -e HOME=/root \
    -v \"/$(pwd):/working/$sketch_dir\" \
    -v \"/$(pwd)/$local_cache_path:/working/build_core\" \
    -v \"${library_host_path}:/shared_libs\" \
    -v \"/$(pwd)/arduino/boards.local.txt:/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/boards.local.txt\" \
    -v \"/$(pwd)/arduino/tvmg_littlefs_16MB.csv:/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/tools/partitions/tvmg_littlefs_16MB.csv\" \
    -v \"/$(pwd)/arduino/tvmg_ota_512KB_spiffs_4MB.csv:/root/.arduino15/packages/esp32/hardware/esp32/3.1.0/tools/partitions/tvmg_ota_512KB_spiffs_4MB.csv\" \
    -w \"/working/$sketch_dir\" \
    tvmg-builder"

    # build the application image
  printf "=======================================================\n"
  printf "Sketch: $sketch_dir | BOARD: $board_name\n"
  printf "=======================================================\n\n"

  local cmd="mkdir -p /working/build_core/work; \
             rm -f /working/build_core/work/partitions.csv; \
             arduino-cli compile --jobs $jobs_num $verbose_flag \
             --build-path /working/build_core/work \
             --libraries /shared_libs \
             --fqbn $internal_fqbn \
             --output-dir /working/$sketch_dir/$local_output_path ."

  eval "$DOCKER_BASE /bin/bash -c '$cmd'"
  local build_status=$?

  # Post-Build: Rename the application binary given the board name
  if [ $build_status -eq 0 ]; then
    local source_bin="${local_output_path}/${sketch_dir}.ino.bin"
    local target_bin="${local_output_path}/${board_name}.bin"

    if [ -f "$source_bin" ]; then
      mv "$source_bin" "$target_bin"
      printf " Success, generated: $target_bin\n\n"
      build_status=0
    fi
  else
    printf "\n\n Build failed with exit code $build_status\n\n"
  fi

  # Generate full flash image if ok
  if [[ $build_status -eq 0 && "$factory_mode" == true ]]; then
    eval "$DOCKER_BASE /bin/bash ./scripts/gen_factory_image.sh $board_name"
    build_status=$?
  fi

  # Copy to Windows output
  if [[ $build_status -eq 0 && ! -z "$win_docs" ]]; then
    local windows_dest="${win_docs}/tvmg/${board_name}"

    mkdir -p "${windows_dest}"
    cp "${local_output_path}/${board_name}"* "${windows_dest}"

    printf "\n${windows_dest} updated\n\n"
  fi

  return $build_status
}

# Some alias's for building

alias espbuild-32='espbuild tvmg_esp32'
alias espbuild-s3='espbuild tvmg_esp32s3'
alias espbuild-ws='espbuild tvmg_wsharelcd'
alias espbuild-wr='espbuild tvmg_wsrelay'


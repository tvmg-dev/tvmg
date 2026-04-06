function espbuild()
{
  # 1. Parameter Handling
  local boardName=""
  local factoryMode=false   # Changed default to false
  local buildMain=true      # Added to toggle main image compilation
  local releaseMode=false   # New release option
  local cleanRequested=false # Track if -c or -a was used
  local verbosity="--verbose"
  local dockerUser=""
  local numJobs=4

  for arg in "$@"; do
    case $arg in
      -h)
        printf "Usage: espbuild [options] [boardName]\n"
        printf " -h : display help\n"
        printf " -c : clean (remove specific board cache and output folders)\n"
        printf " -f : build factory image only\n"
        printf " -a : clean, main image and factory image (all)\n"
        printf " -r : release, copy binary to ~/projects/otatest/binaries with version suffix\n"
        return 0
        ;;
      -c)
        cleanRequested=true
        buildMain=false
        ;;
      -f)
        factoryMode=true
        buildMain=false
        ;;
      -a)
        cleanRequested=true
        factoryMode=true
        buildMain=true
        ;;
      -r)
        releaseMode=true
        ;;
      -factory)
        factoryMode=true
        ;;
      *)
        boardName="$arg"
        ;;
    esac
  done

  # Default board if none provided
  boardName="${boardName:-tvmg_wsharelcd}"
  local fqbn="esp32:esp32:${boardName}"
  local sketchDirectory=$(basename "$(pwd)")

  # Output paths - for the build cache and final artefacts
  local buildCache="build/${boardName}/cache"
  local buildOutput="build/${boardName}/output"
  
  local localCachePath="$(pwd)/${buildCache}"
  local localOutputPath="$(pwd)/${buildOutput}"
  
  # Handle cleaning for the specific board only
  if [ "$cleanRequested" = true ]; then
    printf "Cleaning build folders for: ${boardName}...\n"
    rm -rf "build/${boardName}"
    # If only cleaning was requested (not -a), exit after clean
    if [ "$buildMain" = false ] && [ "$factoryMode" = false ]; then return 0; fi
  fi

  local win_docs_raw=$(powershell.exe -Command "[Environment]::GetFolderPath('MyDocuments')" 2>/dev/null | tr -d '\r')
  local win_docs=$(wslpath "$win_docs_raw" 2>/dev/null)

  local localLibraryPath="${win_docs}/Arduino/libraries"

  if [[ "$OSTYPE" == "linux-gnu"* ]]; then
      printf "Linux environment\n"
      localLibraryPath="$(pwd)/libraries"
      dockerUser="-u $(id -u):$(id -g)"
  fi

  if [ ! -d "$localCachePath" ]; then printf "Make cache directory: $localCachePath\n"; mkdir -p "$localCachePath"; fi
  printf "Using: ${localOutputPath}\n\n"

  # When arduino-cli is generating dependencies then the MMD compiler option may place 2 short
  # header paths on 1 line in the '.d' deps file which arduino-cli then fails to parse correctly.
  # We work around this problem by mounting our working directory beneath a long path, so
  # absolute pathnames prevent the 2 include paths per deps entry.

  local dockerRoot="/working/alongdirectorname/${sketchDirectory}"
  local dockerBuildCache="${dockerRoot}/${buildCache}"
  local dockerOutputPath="${dockerRoot}/${buildOutput}"
  local dockerLibraries="/opt/arduino/libraries"
  
  local arduinoESP32Root="/opt/arduino/packages/esp32/hardware/esp32/3.1.0"

  local DOCKER_BASE="MSYS_NO_PATHCONV=1 docker run ${dockerUser} --rm \
    -e HOME=/root \
    -v \"$(pwd):${dockerRoot}\" \
    -w \"${dockerRoot}\" \
    tvmg-builder"

  local status=0

  # Only build main image if not in factory-only mode
  if [ "$buildMain" = true ]; then
    rm -rf "${localOutputPath}"
    mkdir -p "$localOutputPath"

    # build the application image
    printf "=======================================================\n"
    printf "Sketch: $sketchDirectory | BOARD: $boardName\n"
    printf "=======================================================\n\n"

    # boards.local.txt has to have timestamps preserved, but not the partitions but we will
    # preserve them anyway.  The docker container has the associatred parent directories
    # set with 777 permissions to allow non-root user to modify.

    # for tracing the build add : --log-level trace --log --no-color \

    local cmd="\
            cp -p ./arduino/boards.local.txt ${arduinoESP32Root} && \
            cp -p ./arduino/*.csv ${arduinoESP32Root}/tools/partitions && \
            arduino-cli compile --jobs $numJobs $verbosity \
            --build-path ${dockerBuildCache} \
            --libraries ${dockerLibraries} \
            --fqbn $fqbn \
            --output-dir ${dockerOutputPath} ${dockerRoot}"

    { time eval "$DOCKER_BASE /bin/bash -c '$cmd'"; } 2>&1 | tee "${buildOutput}/build.log"
    status=${PIPESTATUS[0]}

    # Post-Build: Rename the application binary given the board name
    if [ $status -eq 0 ]; then
      local source_bin="${localOutputPath}/${sketchDirectory}.ino.bin"
      local target_bin="${localOutputPath}/${boardName}.bin"

      if [ -f "$source_bin" ]; then
        mv "$source_bin" "$target_bin"
        printf "\n\nSuccess, generated: $target_bin\n\n"
        status=0

        if [ "$releaseMode" = true ]; then
          local version=$(perl -nle 'print $1 and exit if /^\s*const\s+char\s*\*\s*k_versionStr\s*=\s*"([^"]*)"/;' src/config/Config.cpp)
          if [ -z "$version" ]; then
            printf "Error: could not extract k_versionStr from src/config/Config.cpp\n"
            return 1
          fi

          local releaseDir="$HOME/projects/otatest/binaries"
          mkdir -p "$releaseDir"

          local release_bin="${releaseDir}/${boardName}-${version}.bin"
          cp -p "$target_bin" "$release_bin"
          printf "Release binary copied to: %s\n" "$release_bin"

          local release_md5="${release_bin}.md5"
          md5sum "$release_bin" | awk '{print $1}' > "$release_md5"
          printf "Release checksum written to: %s\n" "$release_md5"
        fi
      fi
    else
      printf "\n\nBuild failed with exit code $status\n\n"
    fi
  fi

  # Generate full flash image if ok
  if [[ $status -eq 0 && "$factoryMode" == true ]]; then
    eval "$DOCKER_BASE /bin/bash ./scripts/gen_factory_image.sh $boardName"
    status=$?
  fi

  # Copy to Windows output
  if [[ $status -eq 0 && ! -z "$win_docs" ]]; then
    local windows_dest="${win_docs}/tvmg/${boardName}"

    mkdir -p "${windows_dest}"
    cp "${localOutputPath}/${boardName}"* "${windows_dest}"

    printf "\nWindows: ${windows_dest} updated\n\n"
  fi

  return $status
}

# Some alias's for building

alias espbuild-32='espbuild tvmg_esp32'
alias espbuild-s3='espbuild tvmg_esp32s3'
alias espbuild-ws='espbuild tvmg_wsharelcd'
alias espbuild-wr='espbuild tvmg_wsrelay'

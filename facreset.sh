#!/bin/bash

# Hard coded for now, ESP32-S3 will have different loader

bootloaderOffset="0x1000"
partitionsOffset="0x8000"

# env variables: ESPTOOL_BAUD, ESPTOOL_CHIP,ESPTOOL_PORT

export ESPTOOL_BAUD=921600
export ESPTOOL_CHIP="esp32"
export ESPTOOL_PORT="COM3"

#$ESPTOOL --chip esp32 --port "COM3" --baud 921600
#esptool --chip esp32 --port "COM3" --baud 921600  --before default_reset --after hard_reset write_flash  -z --flash_mode keep --flash_freq keep --flash_size keep 0x1000 %cd%\bootloader.bin 0x8000 %cd%\partitions.bin 0xe000 %cd%\boot_app0.bin 0x10000

declare -a requiredFiles=( "app.bin" "bootloader.bin" "boot_app0.bin" "partitions.bin" "spiffs.bin" )

getOffset()
{
   local off=$($PARTTOOL -f partitions.bin get_partition_info --partition-name $1 | awk '{print $1'})
   if [ -z "$off" ]; then
      aborting "Offset: No $1 partition found in partitions.bin"
   fi

   echo $off
}

getSize()
{
   local size=$($PARTTOOL -f partitions.bin get_partition_info --partition-name $1 | awk '{print $2'})
   if [ -z "$size" ]; then
      aborting "Size: No $1 partition found in partitions.bin"
   fi

   echo $size
}

aborting()
{
   printf "\n   $@,aborting...\n\n"
   exit 1
}

#--------------------------------------------

if [ ! -d spiffs ]; then
   aborting "No spiffs directory"
fi

spiffsSize=$(getSize spiffs)

printf "Making spiffs.bin [size = $spiffsSize] from :\n\n"
rm -f spiffs.bin

ls spiffs

$SPIFFSGEN --block-size 4096 --page-size 256 "$spiffsSize" spiffs spiffs.bin

if [ $? -ne 0 ]; then
   aborting "Error in SPIFFS generation"
fi

printf "\nSPIFFS generation complete.\n\n"

#---------------------------------------------

for file in "${requiredFiles[@]}"
do
   if [ ! -f "$file" ]; then
      aborting "$file not present"
   fi
done

# Now get some partition information

otaOffset=$(getOffset otadata)
app0Offset=$(getOffset app0)
app1Offset=$(getOffset app1)
spiffsOffset=$(getOffset spiffs)

printf "Flash partioning as\n\n"
printf "  ldr    : $bootloaderOffset\n"
printf "  part   : $partitionsOffset\n"
printf "  ota    : $otaOffset\n"
printf "  app0   : $app0Offset\n"
printf "  app1   : $app1Offset\n"
printf "  spiffs : $spiffsOffset\n\n"

# Get flash params from the app binary

$ESPTOOL -t image_info -v 2 app.bin | tr -cd '\11\12\15\40-\176' > image.info

flashFreq=$(grep "Flash freq:" image.info | awk '{print $3}')
flashMode=$(grep "Flash mode:" image.info | awk '{print $3}' | tr '[:upper:]' '[:lower:]')
flashSize=$(grep "Flash size:" image.info | awk '{print $3}')

printf "Flash programming with\n\n"
printf "  size   : $flashSize\n"
printf "  mode   : $flashMode\n"
printf "  freq   : $flashFreq\n\n"

# Generate a full image for programming

$ESPTOOL --chip "$ESPTOOL_CHIP" merge_bin -o final.bin \
   --fill-flash-size "$flashSize" --flash_mode "$flashMode" --flash_freq "$flashFreq" --flash_size "$flashSize" \
   $bootloaderOffset bootloader.bin \
   $partitionsOffset partitions.bin \
   $otaOffset boot_app0.bin \
   $app0Offset app.bin \
   $app1Offset app.bin \
   $spiffsOffset spiffs.bin


# So we program either the 'arduino' way, i.e. flashing the differrent partitions
# - we add a bit to program the second app slot too. Shouldn't make any notional
# difference as the boot_app0 probably indicates valid bootable image in slot
# zero effectively.
#
# The other route is to flash the 'clean' full image, which will clear down
# all of the flash - so any nvs key-pairs that have been used, core dumps etc

if [ 0 -eq 1 ]; then
   $ESPTOOL --chip "$ESPTOOL_CHIP" --port "$ESPTOOL_PORT" --baud "$ESPTOOL_BAUD" \
      --before default_reset --after hard_reset write_flash  -z \
      --flash_mode "$flashMode" --flash_freq "$flashFreq" --flash_size "$flashSize" \
      $bootloaderOffset bootloader.bin \
      $partitionsOffset partitions.bin \
      $otaOffset boot_app0.bin \
      $app0Offset app.bin \
      $app1Offset app.bin \
      $spiffsOffset spiffs.bin
else
   $ESPTOOL --chip esp32 --port "COM3" --baud 921600 \
      --before default_reset --after hard_reset write_flash \
      --flash_mode dio --flash_freq 80m --flash_size 4MB \
      0 final.bin
fi

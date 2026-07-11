#!/bin/bash

LIBPATH=/opt/arduino/libraries

mkdir -p "$LIBPATH"
rm -rf "$LIBPATH"/*

cd "$LIBPATH"

# parameter 1 is the archive, parameter 2 is the path to store

getPackage()
{
   package=$1
   path=$2

   mkdir -p "$path"
   curl -L "$package" | tar -xzp --strip-components=1 -C "$path"
   
   if [ $? -ne 0 ]; then
      printf "\n\nFailed to retrieve $package, or store to $path\n\n"
      exit 1
   fi

   # remove carriage return line endings
   
   find "$path" -type f -exec sed -i 's/\r$//' {} +
}

getReadyMail()
{
   mkdir ReadyMail
   tar -xzp --strip-components=1 -f /root/ReadyMail-0.3.6.tar.gz -C ReadyMail
   rm -f /root/ReadyMail-0.3.6.tar.gz
}


getPackage https://github.com/mathieucarbou/AsyncTCP/archive/refs/tags/v3.3.1.tar.gz AsyncTCP
getPackage https://github.com/milesburton/Arduino-Temperature-Control-Library/archive/refs/tags/3.9.0.tar.gz DallasTemperature
getPackage https://github.com/mathieucarbou/ESPAsyncWebServer/archive/refs/tags/v3.4.5.tar.gz ESP_Async_WebServer
getPackage https://github.com/4-20ma/ModbusMaster/archive/refs/tags/v2.0.1.tar.gz ModbusMaster
getPackage https://github.com/PaulStoffregen/OneWire/archive/refs/tags/v2.3.8.tar.gz OneWire
getPackage https://github.com/emelianov/modbus-esp8266/archive/refs/tags/4.1.0.tar.gz modbus-esp8266
getPackage https://github.com/olikraus/U8g2_Arduino/archive/refs/tags/2.35.30.tar.gz U8g2

# Ready mail is unique, the owner removed the archive so we have to 
getReadyMail

# Apply patches (ignore white space errors)

patch -l -p1 -d AsyncTCP < /tmp/patches/AsyncTCP.patch && \
patch -l -p1 -d ModbusMaster < /tmp/patches/ModbusMaster.patch && \
patch -l -p1 -d ReadyMail < /tmp/patches/readymail.patch

if [ $? -eq 0 ];then
   printf "\n  Patched successfully\n"
   exit 0
fi

exit 1



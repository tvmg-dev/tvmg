mkspiffs -c %cd%\spiffs -b 4096 -p 256 -s 0x160000 %cd%\spiffs.bin

esptool --chip esp32 --port "COM3" --baud 921600  --before default_reset --after hard_reset write_flash  -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 %cd%\bootloader.bin 0x8000 %cd%\partitions.bin 0xe000 %cd%\boot_app0.bin 0x10000 %cd%\app.bin 0x150000 %cd%\app.bin 0x290000 %cd%\spiffs.bin

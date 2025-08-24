#!/bin/bash

printf "Core\n"
grep "Using" libraries.txt | grep "esp32" | sed 's#in folder.*##g' | sed 's#Using library ##g' | sort

printf "\nCustom\n"
grep "Using" libraries.txt | grep "Documents" | sed 's#in folder.*##g' | sed 's#Using library ##g' | sort

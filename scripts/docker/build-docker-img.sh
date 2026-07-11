#!/bin/bash

cp -r ../../oss/patches .

docker build --no-cache -t tvmg-builder . 2>&1 | tee docker-build.log
rm -rf patches



#!/bin/bash

docker build --no-cache -t tvmg-doc . 2>&1 | tee docker-build.log

# docker run -p 4000:4000 -v "$(pwd):/working" tvmg-doc


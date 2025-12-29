#!/bin/bash

docker run -v $(pwd):/home/swr_calculator -w /home/swr_calculator -t wichtounet/cpp:alpine-latest make clean

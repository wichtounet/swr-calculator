#!/bin/bash

docker run -v $(pwd):/home/swr_calculator -w /home/swr_calculator -t swr:build make clean

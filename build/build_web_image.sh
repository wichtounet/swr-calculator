#!/bin/bash

docker run -v $(pwd):/home/swr_calculator -w /home/swr_calculator -t wichtounet/swr:build make clean
docker run -v $(pwd):/home/swr_calculator -w /home/swr_calculator -t wichtounet/swr:build make -j9 release_debug
docker build -f build/Dockerfile.web -t swr:web .
docker tag swr:web wichtounet/swr:web
docker push wichtounet/swr:web

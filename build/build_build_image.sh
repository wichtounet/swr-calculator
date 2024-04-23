#!/bin/bash

set -e

docker build -f build/Dockerfile.build -t swr:build .
docker tag swr:build wichtounet/swr:build
docker push wichtounet/swr:build

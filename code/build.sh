#!/usr/bin/zsh

mkdir -p ../../build
pushd ../../build
g++ ../handmade/code/sdl_handmade.cpp -o handmadehero -g `sdl2-config --cflags --libs`
popd

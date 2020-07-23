#!/usr/bin/zsh

mkdir -p ../../build
pushd ../../build
c++ -DHANDMADE_INTERNAL=1 -DHANDMADE_SLOW=1 ../handmade/code/sdl_handmade.cpp -o handmadehero -g `sdl2-config --cflags --libs`
popd

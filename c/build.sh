#!/usr/bin/env bash

if [ -z $1 ]; then
    echo "Target unspecified"
    exit 1
fi

if [ $1 == "dbg" ]; then
    flags="-g -O0 -std=c99 -pedantic -Wall -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lm"
    out="./out/debug.exe"
    echo "Building debug..."
elif [ $1 == "rls" ]; then
    flags="-O3 -std=c99 -ffast-math -march=native -fopenmp -pedantic -Wall -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf  -lm"
    out="./out/release.exe"
    echo "Building release..."
else
    echo "Invalid target"
    exit 1
fi

######################################
files="main.c"
######################################

gcc $files $flags -o $out

if [ $? == 1 ]; then
    echo "Build failed."
    exit 1
else
    echo "Build success."
fi

if [ -z $2 ]; then
    exit 0
else
    echo "Running..."
    echo "-------------------------------"

    ./$out
fi

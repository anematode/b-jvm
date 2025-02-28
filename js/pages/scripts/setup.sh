#!/bin/zsh
cd src
ln -s ../../bjvm.ts bjvm.ts

mkdir -p ../build
ln -s ../../../build/bjvm_main.js ../build/bjvm_main.js
ln -s ../../../build/bjvm_main.d.ts ../build/bjvm_main.d.ts

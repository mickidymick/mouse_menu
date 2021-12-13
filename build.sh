#!/bin/bash
gcc -o mouse-menu.so mouse-menu.c $(yed --print-cflags) $(yed --print-ldflags)

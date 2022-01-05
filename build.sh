#!/bin/bash
gcc -o mouse_menu.so mouse_menu.c $(yed --print-cflags) $(yed --print-ldflags)

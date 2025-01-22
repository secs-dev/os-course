#!/bin/bash
sudo make;
sudo insmod source/vtfs.ko;
sudo mount -t vtfs "TODO" /mnt/vt;


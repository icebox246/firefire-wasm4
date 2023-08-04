#!/bin/sh

# build resources
w4 png2src --c sprites/art.png -o src/art.h
w4 png2src --c sprites/bg2.png -o src/bg2.h
w4 png2src --c sprites/bg3.png -o src/bg3.h
w4 png2src --c sprites/bg4.png -o src/bg4.h
w4 png2src --c sprites/fireball.png -o src/fireball.h
w4 png2src --c sprites/mage.png -o src/mage.h

# build the actual cart
make

#!/bin/bash

CELLAR=$HOMEBREW_PREFIX/Cellar
PYTHON=$HOMEBREW_PREFIX/opt/python/libexec/bin/python

LDFLAGS=-L/opt/homebrew/lib \
    $PYTHON setup.py \
    -I${CELLAR}/librsync/2.3.4/include \
    -I${CELLAR}/xxhash/0.8.2/include \
    -I${CELLAR}/python@3.12/3.12.0/Frameworks/Python.framework/Versions/3.12/eeaders

rm -rf /Applications/kitty.app
cp -r kitty.app /Applications/Alacritty.app

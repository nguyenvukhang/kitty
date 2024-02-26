RG := \rg
RG += --iglob='!alatty/gl-wrapper.h'
RG += --iglob='!*.txt'
RG += '[^a-z0-9_]5[^a-z0-9]'

current: build c

build:
	python3 build.py

c:
	./alatty.app/Contents/MacOS/alatty

open:
	open alatty.app

size:
	-du -sh ./alatty.app
	-du -s ./alatty.app

.PHONY: build

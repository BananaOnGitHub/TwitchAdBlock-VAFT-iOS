.PHONY: all build test verify clean deb release

all: build

build:
	./build.sh

test:
	python3 -m unittest discover -s tests -v

verify: build
	python3 tools/verify_macho.py build/TwitchAdBlock.dylib

deb: verify
	python3 tools/build_deb.py

clean:
	rm -rf build dist

release: verify test deb
	python3 tools/package_release.py

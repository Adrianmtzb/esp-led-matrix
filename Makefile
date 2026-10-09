SKETCH  := firmware/ledmatrix
BOARD   ?= s3
BUILD   := build/$(BOARD)

FQBN_c6 := esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M
FQBN_s3 := esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M,PSRAM=opi
FQBN    := $(FQBN_$(BOARD))
CORE_VERSION := 3.2.0

# Pass PORT=/dev/cu.usbmodemXXXX when more than one board is connected.
PORT    ?= $(shell ls /dev/cu.usbmodem* 2>/dev/null | head -n1)

# Landing URLs. CI passes the real ones; locally they default to the git remote, if any.
REPO_URL ?= $(shell git remote get-url origin 2>/dev/null | sed -E 's|^git@github.com:|https://github.com/|; s|\.git$$||')
REPO_URL := $(or $(REPO_URL),https://github.com/OWNER/esp-led-matrix)
OWNER    := $(word 3,$(subst /, ,$(REPO_URL)))
REPO_NAME:= $(word 4,$(subst /, ,$(REPO_URL)))
SITE_URL ?= http://localhost:8000/

.PHONY: deps gen check build build-all flash monitor shot bootapp0 dist dist-all og site serve bump mcp-install clean

deps:
	arduino-cli core install esp32:esp32@$(CORE_VERSION)
	arduino-cli lib install "GFX Library for Arduino@1.6.4" "ArduinoJson@7.4.2"

gen:
	node tools/gen_assets.mjs
	node tools/gen_web.mjs

check:
	node tools/gen_assets.mjs --check
	node tools/gen_web.mjs --check
	python3 scripts/check-manifest.py
	node tools/check_consistency.mjs

build: gen
	@test -n "$(FQBN)" || (echo "BOARD must be c6 or s3" && exit 1)
	arduino-cli compile --fqbn $(FQBN) --build-path $(BUILD) $(SKETCH)

# Compiled in series: parallel builds of one sketch collide in the arduino-cli cache.
build-all:
	$(MAKE) build BOARD=c6
	$(MAKE) build BOARD=s3

flash: build
	arduino-cli upload --fqbn $(FQBN) --input-dir $(BUILD) -p $(PORT) $(SKETCH)

monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=115200

shot:
	python3 tools/screenshot.py $(PORT) shot-$(BOARD).png

# boot_app0.bin is not produced by the compiler: copy it from the installed core.
bootapp0:
	cp "$$(arduino-cli config get directories.data)/packages/esp32/hardware/esp32/$(CORE_VERSION)/tools/partitions/boot_app0.bin" $(BUILD)/boot_app0.bin

# Binaries renamed to <part>-<board>.bin, as docs/manifest.json expects. Needs a previous build.
dist: bootapp0
	mkdir -p dist
	cp $(BUILD)/ledmatrix.ino.bootloader.bin dist/bootloader-$(BOARD).bin
	cp $(BUILD)/ledmatrix.ino.partitions.bin dist/partitions-$(BOARD).bin
	cp $(BUILD)/boot_app0.bin dist/boot_app0-$(BOARD).bin
	cp $(BUILD)/ledmatrix.ino.bin dist/ledmatrix-$(BOARD).bin
	cp $(BUILD)/ledmatrix.ino.merged.bin dist/ledmatrix-merged-$(BOARD).bin

dist-all: build-all
	$(MAKE) dist BOARD=c6
	$(MAKE) dist BOARD=s3

og:
	node tools/gen_og.mjs _site/og.png

# Same _site/ the Pages job deploys. NOBUILD=1 reuses the binaries already in dist/ (CI).
site:
	@test "$(NOBUILD)" = 1 || $(MAKE) dist-all
	rm -rf _site && mkdir -p _site/shared
	sed -e 's|__SITE_URL__|$(SITE_URL)|g' -e 's|__REPO_URL__|$(REPO_URL)|g' \
	    -e 's|__REPO_NAME__|$(REPO_NAME)|g' -e 's|__OWNER__|$(OWNER)|g' docs/index.html > _site/index.html
	cp docs/manifest.json _site/
	cp shared/*.json _site/shared/
	cp dist/bootloader-*.bin dist/partitions-*.bin dist/boot_app0-*.bin dist/ledmatrix-c6.bin dist/ledmatrix-s3.bin _site/
	$(MAKE) og

# Web Serial only works over HTTPS or on localhost.
serve:
	python3 -m http.server 8000 -d _site

bump:
	@test -n "$(VERSION)" || (echo "usage: make bump VERSION=X.Y.Z" && exit 1)
	sed -i.bak -E 's/#define FW_VERSION "[^"]+"/#define FW_VERSION "$(VERSION)"/' $(SKETCH)/config.h && rm $(SKETCH)/config.h.bak
	sed -i.bak -E 's/"version": "[^"]+"/"version": "$(VERSION)"/' docs/manifest.json && rm docs/manifest.json.bak
	python3 scripts/check-manifest.py

mcp-install:
	cd mcp && npm install

clean:
	rm -rf build dist _site

TITLE      := Stremio
VERSION    := 9.10
TITLE_ID   := BREW00100
CONTENT_ID := IV0000-BREW00100_00-STREMIOPS4000000

TOOLCHAIN  := $(OO_PS4_TOOLCHAIN)
COMMONDIR  := $(TOOLCHAIN)/samples/_common
BUILDDIR   := build
DISTDIR    := dist
TARGET     := stremio-ps4

RIGHT_SPRX ?= $(TOOLCHAIN)/samples/hello_world/sce_sys/about/right.sprx
ICON0      := app/sce_sys/icon0.png
LIBC_PRX   ?= $(TOOLCHAIN)/samples/graphics/sce_module/libc.prx
FIOS2_PRX  ?= $(TOOLCHAIN)/samples/graphics/sce_module/libSceFios2.prx
UI_FONT    := app/fonts/Inter-Regular.ttf
PS5_APP_ASSETS := app

CC         := clang-18
CXX        := clang++-18
LD         := ld.lld-18
TOOLS      := $(TOOLCHAIN)/bin/linux

LIBS       := -lc -lkernel -lc++ -lSceVideoOut -lSceSysmodule \
	-lScePad -lSceUserService -lSceSysUtil -lSceSystemService \
	-lSceNet -lSceSsl -lSceHttp
LIBS       += -lSceCommonDialog -lSceImeDialog
CFLAGS     := --target=x86_64-pc-freebsd12-elf -O2 -DNDEBUG \
	-fPIC -funwind-tables -c \
	-isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS   := $(CFLAGS) -isystem $(TOOLCHAIN)/include/c++/v1 \
	-I$(COMMONDIR) -I$(TOOLCHAIN)/include/stb -Isrc -Isrc/rebuild
LDFLAGS    := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x \
	--eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

OBJECTS    := $(BUILDDIR)/ps5port_app.o $(BUILDDIR)/rebuild_platform.o \
	$(BUILDDIR)/ps5port_shell.o $(BUILDDIR)/ps5port_artwork.o \
	$(BUILDDIR)/rebuild_stremio.o $(BUILDDIR)/graphics.o \
	$(BUILDDIR)/poster.o $(BUILDDIR)/catalog.o
PACKAGE    := $(DISTDIR)/$(CONTENT_ID).pkg

.PHONY: all prepare package check clean

all: prepare

prepare: check $(BUILDDIR)/pkg.gp4

package:
	@test -f "$(BUILDDIR)/pkg.gp4" || (echo "Run make prepare first"; exit 1)
	mkdir -p $(DISTDIR)
	$(TOOLS)/PkgTool.Core pkg_build $(BUILDDIR)/pkg.gp4 $(DISTDIR)

check:
	@echo "Release optimization: -O2"
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "OO_PS4_TOOLCHAIN is not set"; exit 1)
	@test -f "$(TOOLCHAIN)/link.x" || (echo "OpenOrbis link.x not found"; exit 1)
	@test -f "$(COMMONDIR)/graphics.cpp" || (echo "OpenOrbis common graphics source not found"; exit 1)
	@test -f "$(RIGHT_SPRX)" || (echo "right.sprx not found; set RIGHT_SPRX"; exit 1)
	@test -f "$(ICON0)" || (echo "icon0.png not found; set ICON0"; exit 1)
	@test -f "$(LIBC_PRX)" || (echo "libc.prx not found; set LIBC_PRX"; exit 1)
	@test -f "$(FIOS2_PRX)" || (echo "libSceFios2.prx not found; set FIOS2_PRX"; exit 1)
	@test -f "$(UI_FONT)" || (echo "UI font not found: $(UI_FONT)"; exit 1)
	@test -d "$(PS5_APP_ASSETS)/assets/icons_4k" || (echo "Vendored PS5 UI assets not found"; exit 1)

$(BUILDDIR) $(DISTDIR) $(BUILDDIR)/sce_sys/about $(BUILDDIR)/sce_module:
	mkdir -p $@

$(BUILDDIR)/ps5port_app.o: src/ps5port/app.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/rebuild_platform.o: src/rebuild/sony_platform.cpp src/rebuild/sony_platform.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/ps5port_shell.o: src/ps5port/port_shell.cpp src/ps5port/port_shell.h src/ps5port/ui_model.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/ps5port_artwork.o: src/ps5port/artwork_service.cpp src/ps5port/artwork_service.h src/poster.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/rebuild_stremio.o: src/rebuild/stremio_service.cpp src/rebuild/stremio_service.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/graphics.o: src/graphics.cpp src/graphics.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/catalog.o: src/catalog.cpp src/catalog.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/poster.o: src/poster.cpp src/poster.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/$(TARGET).elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILDDIR)/eboot.bin: $(BUILDDIR)/$(TARGET).elf
	$(TOOLS)/create-fself -in=$< -out=$(BUILDDIR)/$(TARGET).oelf \
		--eboot $@ --paid 0x3800000000000011

$(BUILDDIR)/sce_sys/about/right.sprx: $(RIGHT_SPRX) | $(BUILDDIR)/sce_sys/about
	cp $< $@

$(BUILDDIR)/sce_sys/icon0.png: $(ICON0) | $(BUILDDIR)/sce_sys/about
	cp $< $@

$(BUILDDIR)/sce_module/libc.prx: $(LIBC_PRX) | $(BUILDDIR)/sce_module
	cp $< $@

$(BUILDDIR)/sce_module/libSceFios2.prx: $(FIOS2_PRX) | $(BUILDDIR)/sce_module
	cp $< $@

$(BUILDDIR)/assets/Gontserrat-Regular.ttf: $(UI_FONT)
	mkdir -p $(BUILDDIR)/assets
	cp $< $@
	cp $(PS5_APP_ASSETS)/assets/icons_4k/* $(BUILDDIR)/assets/
	cp $(PS5_APP_ASSETS)/fonts/* $(BUILDDIR)/assets/
	cp $(PS5_APP_ASSETS)/sounds/glass/* $(BUILDDIR)/assets/
	cp $(PS5_APP_ASSETS)/licenses/* $(BUILDDIR)/assets/
	cp $(PS5_APP_ASSETS)/ca-bundle.crt $(BUILDDIR)/assets/ca-bundle.crt

$(BUILDDIR)/sce_sys/param.sfo: Makefile | $(BUILDDIR)/sce_sys/about
	$(TOOLS)/PkgTool.Core sfo_new $@
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

$(BUILDDIR)/pkg.gp4: $(BUILDDIR)/eboot.bin $(BUILDDIR)/sce_sys/about/right.sprx \
	$(BUILDDIR)/sce_sys/icon0.png $(BUILDDIR)/sce_sys/param.sfo \
	$(BUILDDIR)/sce_module/libc.prx $(BUILDDIR)/sce_module/libSceFios2.prx \
	$(BUILDDIR)/assets/Gontserrat-Regular.ttf
	cd $(BUILDDIR) && $(TOOLS)/create-gp4 -out pkg.gp4 \
		--content-id=$(CONTENT_ID) \
		--files "eboot.bin sce_sys/about/right.sprx sce_sys/icon0.png sce_sys/param.sfo sce_module/libc.prx sce_module/libSceFios2.prx $$(find assets -type f -print | tr '\n' ' ')"

$(PACKAGE): $(BUILDDIR)/pkg.gp4 | $(DISTDIR)
	$(TOOLS)/PkgTool.Core pkg_build $< $(DISTDIR)

clean:
	rm -rf $(BUILDDIR) $(DISTDIR)

TITLE      := Stremio PS4
VERSION    := 0.01
TITLE_ID   := BREWSTPS4
CONTENT_ID := IV0000-BREWSTPS4_00-STREMIOPS4000000

TOOLCHAIN  := $(OO_PS4_TOOLCHAIN)
COMMONDIR  := $(TOOLCHAIN)/samples/_common
BUILDDIR   := build
DISTDIR    := dist
TARGET     := stremio-ps4

RIGHT_SPRX ?= $(COMMONDIR)/sce_sys/about/right.sprx
ICON0      ?= $(COMMONDIR)/sce_sys/icon0.png

CC         := clang
CXX        := clang++
LD         := ld.lld
TOOLS      := $(TOOLCHAIN)/bin/linux

LIBS       := -lc -lkernel -lc++ -lSceVideoOut -lSceSysmodule
CFLAGS     := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
	-isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS   := $(CFLAGS) -std=c++17 -isystem $(TOOLCHAIN)/include/c++/v1 \
	-I$(COMMONDIR)
LDFLAGS    := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x \
	--eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

OBJECTS    := $(BUILDDIR)/main.o $(BUILDDIR)/graphics.o
PACKAGE    := $(DISTDIR)/$(CONTENT_ID).pkg

.PHONY: all check clean

all: check $(PACKAGE)

check:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "OO_PS4_TOOLCHAIN is not set"; exit 1)
	@test -f "$(TOOLCHAIN)/link.x" || (echo "OpenOrbis link.x not found"; exit 1)
	@test -f "$(COMMONDIR)/graphics.cpp" || (echo "OpenOrbis common graphics source not found"; exit 1)
	@test -f "$(RIGHT_SPRX)" || (echo "right.sprx not found; set RIGHT_SPRX"; exit 1)
	@test -f "$(ICON0)" || (echo "icon0.png not found; set ICON0"; exit 1)

$(BUILDDIR) $(DISTDIR) $(BUILDDIR)/sce_sys/about:
	mkdir -p $@

$(BUILDDIR)/main.o: src/main.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/graphics.o: $(COMMONDIR)/graphics.cpp | $(BUILDDIR)
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
	$(BUILDDIR)/sce_sys/icon0.png $(BUILDDIR)/sce_sys/param.sfo
	cd $(BUILDDIR) && $(TOOLS)/create-gp4 -out pkg.gp4 \
		--content-id=$(CONTENT_ID) \
		--files "eboot.bin sce_sys/about/right.sprx sce_sys/icon0.png sce_sys/param.sfo"

$(PACKAGE): $(BUILDDIR)/pkg.gp4 | $(DISTDIR)
	$(TOOLS)/PkgTool.Core pkg_build $< $(DISTDIR)

clean:
	rm -rf $(BUILDDIR) $(DISTDIR)


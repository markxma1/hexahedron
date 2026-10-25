# PS3 build (PSL1GHT). Uses the shared code in src/core and src/game unchanged:
# every .cpp there is picked up automatically.
#
#   export PS3DEV=~/SDKs/ps3dev PSL1GHT=$PS3DEV
#   export LD_LIBRARY_PATH=<Cg toolkit>/usr/lib64   (cgcomp needs libCg.so)
#   make            -> hexahedron.elf / .self
#   make pkg        -> hexahedron.pkg (install on the console)
.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

HXDIR		:=	$(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include $(PSL1GHT)/ppu_rules

# Shader compiler without -fastprecision (see tools/build_cgcomp.sh).
ifneq ($(wildcard $(HXDIR)tools/cgcomp-full),)
CGCOMP		:=	$(HXDIR)tools/cgcomp-full
else
$(warning tools/cgcomp-full missing - run tools/build_cgcomp.sh, shaders may render wrong)
endif

TARGET		:=	hexahedron
BUILD		:=	build
ROOT		:=	../../..
SOURCES		:=	. $(ROOT)/src/core $(ROOT)/src/game
SHADERS		:=	shaders
FONTS		:=	$(ROOT)/assets/fonts
INCLUDES	:=	. $(ROOT)/src $(ROOT)/third_party/stb

TITLE		:=	Hexahedron
APPID		:=	HEXA00001
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
ICON0		:=	$(CURDIR)/ICON0.PNG

CFLAGS		=	-O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE) -DHX_NO_THREADS -DHX_PS3
CXXFLAGS	=	$(CFLAGS) -std=gnu++17 $(CXXFLAGS_INIT) -D_GLIBCXX_USE_C99_STDIO -include $(CURDIR)/../ps3_compat.h
LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS		:=	-lrsx -lgcm_sys -lio -lsysutil -laudio -lrt -llv2 -lm

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(SHADERS),$(CURDIR)/$(dir)) \
			$(CURDIR)/$(FONTS)
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
export BUILDDIR	:=	$(CURDIR)/$(BUILD)

CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
VCGFILES	:=	$(foreach dir,$(SHADERS),$(notdir $(wildcard $(dir)/*.vcg)))
FCGFILES	:=	$(foreach dir,$(SHADERS),$(notdir $(wildcard $(dir)/*.fcg)))
TTFFILES	:=	$(notdir $(wildcard $(FONTS)/*.ttf))
VPOFILES	:=	$(VCGFILES:.vcg=.vpo)
FPOFILES	:=	$(FCGFILES:.fcg=.fpo)

export LD	:=	$(CXX)
export OFILES	:=	$(addsuffix .o,$(VPOFILES)) $(addsuffix .o,$(FPOFILES)) \
			$(addsuffix .o,$(TTFFILES)) $(CPPFILES:.cpp=.o)
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) $(LIBPSL1GHT_INC) -I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean pkg

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).fake.self $(OUTPUT).pkg EBOOT.BIN

pkg: $(BUILD) $(OUTPUT).pkg

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

# generated headers (shaders, fonts) must exist before the C++ files compile
$(CPPFILES:.cpp=.o): $(addsuffix .o,$(VPOFILES)) $(addsuffix .o,$(FPOFILES)) $(addsuffix .o,$(TTFFILES))

%.vpo.o: %.vpo
	@echo $(notdir $<)
	@$(bin2o)

%.fpo.o: %.fpo
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif

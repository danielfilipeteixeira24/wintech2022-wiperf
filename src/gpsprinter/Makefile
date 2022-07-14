# Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
# Distributed under the GNU GPL v2. For full terms see the file LICENSE.

# Crosscompilation instructions:
# 1. Set environment variables by running:
#   export STAGING_DIR=/path/to/staging_dir/toolchain-arm_cortex-a15+neon-vfpv4_gcc-5.4.0_musl-1.1.16_eabi/
#   export PATH="$PATH":"$STAGING_DIR"/bin
# 2. Run make ARCH=arm

CFLAGS = -O2 -std=c++17 -Wall --pedantic -fomit-frame-pointer

ifeq ($(ARCH), arm)
	CPP := arm-openwrt-linux-g++
	LIBS :=
else
	CPP := g++
	LIBS := -lrt -lpthread
endif

# directories
# note src dir cannot end in /
SRCDIR := . ../util
BUILDDIR := build
TARGET := $(BUILDDIR)/gpsprinter

SRCEXT := cc
SOURCES := $(shell find $(SRCDIR) -maxdepth 1 -type f -name "*.$(SRCEXT)")
#OBJECTS := $(foreach FILE, $(notdir $(SOURCES:.$(SRCEXT)=.o)), $(BUILDDIR)/$(FILE))
OBJECTS := $(foreach DIR, $(SRCDIR), $(filter-out $(BUILDDIR)/$(DIR)/%, $(filter $(BUILDDIR)/%, $(patsubst $(DIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o)))))

all: $(TARGET)
	@echo "Done!"

$(TARGET): $(OBJECTS)
	@echo "Linking..."
	@echo "  $(CPP) $^ -o $(TARGET) $(LIBS)"; $(CPP) $^ -o $(TARGET) $(LIBS)

.SECONDEXPANSION:
PREREQ = $(foreach DIR, $(SRCDIR), $(filter $(DIR)/$(subst .o,.$(SRCEXT),$(subst $(BUILDDIR)/,,$@)), $(SOURCES)))
$(BUILDDIR)/%.o: $$(PREREQ)
	@mkdir -p $(BUILDDIR)
	@echo "  $(CPP) $(CFLAGS) -c -o $(BUILDDIR)/$(notdir $@) $<"; $(CPP) $(CFLAGS) -c -o $(BUILDDIR)/$(notdir $@) $<

.PHONY: clean
clean:
	@echo "Cleaning...";
	$(RM) -r $(BUILDDIR) $(TARGET) $(OBJECTS)*~

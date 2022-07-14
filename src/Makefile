# Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
# Distributed under the GNU GPL v2. For full terms see the file LICENSE.

# Crosscompilation instructions:
# 1. Set environment variables by running:
#   export STAGING_DIR=/path/to/staging_dir/toolchain-arm_cortex-a15+neon-vfpv4_gcc-5.4.0_musl-1.1.16_eabi/
#   export PATH="$PATH":"$STAGING_DIR"/bin
# 2. Run make ARCH=arm


SUBDIRS = dtransfer gpsprinter mygpsd

.PHONY: subdirs
subdirs:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir; \
	done
    

.PHONY: clean
clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

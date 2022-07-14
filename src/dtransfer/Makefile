# Copyright (c) 2022 Instituto de Telecomunicações, Porto, Portugal, and Contributors.
# Distributed under the GNU GPL v2. For full terms see the file LICENSE.

# Crosscompilation instructions:
# 1. Set environment variables by running:
# export PATH=/path/to/openwrt/staging_dir/toolchain-arm_cortex-a15+neon-vfpv4_gcc-5.4.0_musl-1.1.16_eabi/bin:$PATH
# export STAGING_DIR=/path/to/openwrt/staging_dir/toolchain-arm_cortex-a15+neon-vfpv4_gcc-5.4.0_musl-1.1.16_eabi
# export TARGET_DIR=/path/to/openwrt/staging_dir/target-arm_cortex-a15+neon-vfpv4_musl-1.1.16_eabi
# export TARGET_LIBS=/path/to/openwrt/staging_dir/target-arm_cortex-a15+neon-vfpv4_musl-1.1.16_eabi/usr/lib
# 2. Run make ARCH=arm

SUBDIRS = dreceiver dsender channelMonitor

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

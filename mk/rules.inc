#
# Define all rules for a given config for odp and CUNIT
#
define CONFIG_RULE
#Arg1 = machine name
cunit-$(1)-configure: $(ARCH_DIR)/cunit/$(1)/Makefile

$(eval ODP_$(1)_DOC_INST := $(K1ST_DIR)/doc/ODP/$($(1)_PLATFORM))
$(eval ODP_$(1)_OS_NAME := $(firstword $(subst _, ,$(1))))
$(eval ODP_$(1)_BOARD_NAME := $(lastword $(subst _, ,$(1))))
$(eval ODP_$(1)_ARCH_BUILDDIR := $(ODP_$(1)_BOARD_NAME)/$(ODP_$(1)_OS_NAME))
$(eval ODP_$(1)_CONFCACHEDIR := $(CONFCACHE_DIR)/$(ODP_$(1)_ARCH_BUILDDIR))
$(eval ODP_$(1)_BUILDDIR := $(ARCH_DIR)/odp/$(ODP_$(1)_ARCH_BUILDDIR))
$(eval ODP_$(1)_DEBUG_BUILDDIR := $(ARCH_DIR)/odp-debug/$(ODP_$(1)_ARCH_BUILDDIR))
$(ARCH_DIR)/cunit/$(1)/Makefile: $(TOP_DIR)/cunit/configure
	mkdir -p $(ODP_$(1)_CONFCACHEDIR)
	mkdir -p $$$$(dirname $$@) && cd $$$$(dirname $$@) && \
	$($(1)_CONF_ENV) CPPFLAGS="$($(1)_CFLAGS)" LDFLAGS="$($(1)_LDFLAGS) $($(1)_CFLAGS)" \
	 $$< --srcdir=$(TOP_DIR)/cunit --prefix=$(CUNIT_INST_DIR)/$(1) \
	--enable-debug --enable-automated --enable-basic --host=$(1) \
	--cache-file=$(ODP_$(1)_CONFCACHEDIR)/cunit-config.cache

cunit-$(1)-build: $(ARCH_DIR)/cunit/$(1)/CUnit/Sources/.libs/libcunit.a
$(ARCH_DIR)/cunit/$(1)/CUnit/Sources/.libs/libcunit.a: $(ARCH_DIR)/cunit/$(1)/Makefile
	$$(MAKE) -C$(ARCH_DIR)/cunit/$(1) V=1

cunit-$(1)-install: $(CUNIT_INST_DIR)/$(1)/lib/libcunit.a
$(CUNIT_INST_DIR)/$(1)/lib/libcunit.a: $(ARCH_DIR)/cunit/$(1)/CUnit/Sources/.libs/libcunit.a
	$$(MAKE) -C$(ARCH_DIR)/cunit/$(1) V=1 install

cunit-$(1)-valid:
cunit-$(1)-clean:
	rm -Rf $(ARCH_DIR)/cunit/$(1) $(CUNIT_INST_DIR)/$(1)

odp-debug-$(1)-configure: $(ODP_$(1)_DEBUG_BUILDDIR)/Makefile
$(ODP_$(1)_DEBUG_BUILDDIR)/Makefile: $(TOP_DIR)/configure $(CUNIT_INST_DIR)/$(1)/lib/libcunit.a $(MAKE_DEPS)
	mkdir -p $(ODP_$(1)_CONFCACHEDIR)
	mkdir -p $$$$(dirname $$@) && cd $$$$(dirname $$@) && \
	$($(1)_CONF_ENV) CPPFLAGS="$($(1)_CFLAGS)" LDFLAGS="$($(1)_LDFLAGS) $($(1)_CFLAGS)" \
	 $$< --host=$(1) --with-platform=$($(1)_PLATFORM) \
	--with-cunit-path=$(CUNIT_INST_DIR)/$(1)/ --enable-test-vald \
	--prefix=$(K1ST_DIR) \
	--datarootdir=$(K1ST_DIR)share/odp-debug/tests/$(ODP_$(1)_ARCH_BUILDDIR) \
	--libdir=$(K1ST_DIR)lib/odp-debug/$(ODP_$(1)_ARCH_BUILDDIR) \
	--includedir=$(K1ST_DIR)$($(1)_INC_DIR)/include \
	--enable-test-perf $(DEBUG_FLAGS) $($(1)_CONF_OPTS) --disable-debug-print $(DEBUG_CONF_FLAGS) $(_DEBUG_CONF_FLAGS) \
	--cache-file=$(ODP_$(1)_CONFCACHEDIR)/odp-debug-config.cache

odp-debug-$(1)-build: $(ODP_$(1)_DEBUG_BUILDDIR)/Makefile
	$$(MAKE) -C$$$$(basename $$@)V=1 all
odp-debug-$(1)-clean:
	rm -Rf $(ODP_$(1)_DEBUG_BUILDDIR)

odp-$(1)-configure: $(ODP_$(1)_BUILDDIR)/Makefile
$(ODP_$(1)_BUILDDIR)/Makefile: $(TOP_DIR)/configure $(CUNIT_INST_DIR)/$(1)/lib/libcunit.a $(MAKE_DEPS)
	mkdir -p $(ODP_$(1)_CONFCACHEDIR)
	mkdir -p $$$$(dirname $$@) && cd $$$$(dirname $$@) && \
	$($(1)_CONF_ENV) CPPFLAGS="$($(1)_CFLAGS)" LDFLAGS="$($(1)_LDFLAGS) $($(1)_CFLAGS)" \
	 $$< --host=$(1) --with-platform=$($(1)_PLATFORM) \
	--with-cunit-path=$(CUNIT_INST_DIR)/$(1)/ --enable-test-vald \
	--prefix=$(K1ST_DIR) --enable-user-guides \
	--datarootdir=$(K1ST_DIR)share/odp/tests/$(ODP_$(1)_ARCH_BUILDDIR) \
	--libdir=$(K1ST_DIR)lib/odp/$(ODP_$(1)_ARCH_BUILDDIR) \
	--includedir=$(K1ST_DIR)$($(1)_INC_DIR)/include \
	--enable-test-perf $(DEBUG_FLAGS) $($(1)_CONF_OPTS) --disable-debug-print $(DEBUG_CONF_FLAGS) \
	--cache-file=$(ODP_$(1)_CONFCACHEDIR)/odp-config.cache

odp-$(1)-build: $(ODP_$(1)_BUILDDIR)/Makefile
	$$(MAKE) -C$$$$(dirname $$<) V=1 all

odp-$(1)-install-lib: odp-$(1)-build
	if [ "$($(1)_INSTALL)" == "true" ]; then \
		$$(MAKE) -C$(ODP_$(1)_BUILDDIR) V=1 install; \
		$$(MAKE) -C$(ODP_$(1)_BUILDDIR)/test V=1 install-strip; \
		$$(MAKE) -C$(ODP_$(1)_BUILDDIR)/example V=1 install; \
	else true; fi

odp-$(1)-install-headers: odp-$(1)-configure
	if [ "$($(1)_INSTALL)" == "true" ]; then \
		$$(MAKE) -C$(ODP_$(1)_BUILDDIR)/platform/mppa V=1 install-odprpcincludeHEADERS; \
	else true; fi

odp-$(1)-install-doc: odp-$(1)-build
	if [ "$($(1)_INSTALL_DOC)" == "true" ]; then \
		$$(MAKE) -C$(ODP_$(1)_BUILDDIR) V=1 doxygen-pdf && \
		mkdir -p $(ODP_$(1)_DOC_INST) && \
		install $(ODP_$(1)_BUILDDIR)/doc/output/opendataplane.pdf $(ODP_$(1)_DOC_INST)/opendataplane.pdf && \
		rm -Rf   $(ODP_$(1)_DOC_INST)/opendataplane && \
		mkdir -p $(ODP_$(1)_DOC_INST)/opendataplane && \
		cp -Rf $(ODP_$(1)_BUILDDIR)/doc/output/html/* $(ODP_$(1)_DOC_INST)/opendataplane && \
		$$(MAKE) -C$(ODP_$(1)_BUILDDIR)/doc V=1 all && \
		mkdir -p $(ODP_$(1)_DOC_INST)/images/ && \
		cp -Rf $(ODP_$(1)_BUILDDIR)/doc/images/*.png $(ODP_$(1)_DOC_INST)/images/ && \
		mkdir -p $(ODP_$(1)_DOC_INST)/users-guide/ && \
		cp -Rf $(ODP_$(1)_BUILDDIR)/doc/output/users-guide.html $(ODP_$(1)_DOC_INST)/users-guide/ && \
		true; \
	else true; fi

odp-$(1)-install: odp-$(1)-install-lib odp-$(1)-install-doc

odp-$(1)-valid: odp-$(1)-build extra-install firmware-install
	$$(MAKE) -C$(ODP_$(1)_BUILDDIR)/test/validation $($(1)_MAKE_VALID) check
	$$(MAKE) -C$(ODP_$(1)_BUILDDIR)/platform/$($(1)_PLATFORM)/test/ check
	$$(MAKE) -C$(ODP_$(1)_BUILDDIR)/test/performance $($(1)_MAKE_VALID) check

odp-$(1)-clean:
	rm -Rf $(ODP_$(1)_BUILDDIR)

endef

#
# Define all rules for a given config/firmware
#
define FIRMWARE_RULE
#$(1) CONFIG
#$(2) Firmware type
$(eval $(1)-$(2)-ENV := ODP_FIRMWARE_CONFIG="$(1)" INSTDIR="$(firmware_$(1)_INSTDIR)" ODP_TOOLCHAIN_DIR="$(K1ST_DIR)")
firmware-$(2)-$(1)-configure: firmware/$(2)/Makefile
firmware-$(2)-$(1)-build: firmware/$(2)/Makefile extra-install odp-k1b-kalray-mos_developer-install-headers
	mkdir -p $(ARCH_DIR)/firmware/$(lastword $(subst _, ,$(1)))/$(2)
	cd $(ARCH_DIR)/firmware/$(lastword $(subst _, ,$(1)))/$(2) && $$(MAKE) -f $(TOP_DIR)/firmware/$(2)/Makefile all $($(1)-$(2)-ENV)

firmware-$(2)-$(1)-install: firmware-$(2)-$(1)-build
	cd $(ARCH_DIR)/firmware/$(lastword $(subst _, ,$(1)))/$(2) && $$(MAKE) -f $(TOP_DIR)/firmware/$(2)/Makefile install $($(1)-$(2)-ENV)
firmware-$(2)-$(1)-valid: firmware-$(2)-$(1)-build
	cd $(ARCH_DIR)/firmware/$(lastword $(subst _, ,$(1)))/$(2) && $$(MAKE) -f $(TOP_DIR)/firmware/$(2)/Makefile valid $($(1)-$(2)-ENV)
firmware-$(2)-$(1)-long: firmware-$(2)-$(1)-build
	cd $(ARCH_DIR)/firmware/$(lastword $(subst _, ,$(1)))/$(2) && $$(MAKE) -f $(TOP_DIR)/firmware/$(2)/Makefile long $($(1)-$(2)-ENV)
firmware-$(2)-$(1)-clean:
	rm -Rf $(ARCH_DIR)/firmware/$(lastword $(subst _, ,$(1)))/$(2)
endef


#
# Define rule $(1) for each subtype (arch, firmware, etc.)
#
define SUB_RULES
# $(1) = RULE
# Generate per config rules (clean/build/etc) for each firmware type
$(foreach FIRMWARE, $(FIRMWARES),
firmware-$(FIRMWARE)-$(1): $(foreach CONFIG, $($(FIRMWARE)_CONFIGS), firmware-$(FIRMWARE)-$(CONFIG)-$(1))
CHECK_LIST += $(foreach CONFIG, $($(FIRMWARE)_CONFIGS), firmware-$(FIRMWARE)-$(CONFIG)-$(1))
)

firmware-$(1): $(foreach FIRMWARE, $(FIRMWARES), firmware-$(FIRMWARE)-$(1)) $(firmware-$(1)_DEPS)
CHECK_LIST += $(foreach FIRMWARE, $(FIRMWARES), firmware-$(FIRMWARE)-$(1))

apps-$(1): $(foreach APP, $(APPS), app-$(APP)-$(1))
CHECK_LIST += $(foreach APP, $(APPS), app-$(APP)-$(1))

long-$(1): $(foreach CONFIG, $(filter-out x86_64-unknown-linux-gnu,$(LONG_CONFIGS)), long-$(CONFIG)-$(1))
CHECK_LIST +=  $(foreach CONFIG, $(LONG_CONFIGS), long-$(CONFIG)-$(1))
CHECK_LIST +=  $(foreach APP, $(LONGS), $(foreach CONFIG, $(LONG_CONFIGS), long-$(CONFIG)-$(APP)-$(1)))

# Generate per config rules (clean/build/etc) for each arch specific component
$(foreach ARCH_COMPONENT, $(ARCH_COMPONENTS), \
$(ARCH_COMPONENT)-$(1): $(foreach CONFIG, $(CONFIGS), $(ARCH_COMPONENT)-$(CONFIG)-$(1))
CHECK_LIST += $(foreach CONFIG, $(CONFIGS), $(ARCH_COMPONENT)-$(CONFIG)-$(1))
)
endef

#
# Define all rules for a given app
#
define APP_RULE
#$(1) APP
$(eval $(1)-ENV := CC="$($(1)_CC)" CFLAGS="$($(1)_CFLAGS)" \
	 LDFLAGS="$($(1)_LDFLAGS)")

app-$(1)-configure: apps/$(1)/Makefile
app-$(1)-install: app-$(1)-build
	cd $(ARCH_DIR)/apps/$(1) && $$(MAKE) -f $(TOP_DIR)/apps/$(1)/Makefile install \
	APST_DIR=$(APST_DIR) ODP_TOOLCHAIN_DIR=$(K1ST_DIR)/

app-$(1)-long:
app-$(1)-valid:

app-$(1)-build: apps/$(1)/Makefile
	mkdir -p $(ARCH_DIR)/apps/$(1)
	cd $(ARCH_DIR)/apps/$(1) && $$(MAKE) -f $(TOP_DIR)/apps/$(1)/Makefile \
	ODP_TOOLCHAIN_DIR=$(K1ST_DIR)/ all
app-$(1)-clean:
	rm -Rf $(ARCH_DIR)/apps/$(1)
endef

#
# Define rules for a given long test for a given arch
#
define LONG_RULE
#$(1) CONFIG
#$(2) APP

long-$(1)-$(2)-configure: long/$(2)/Makefile
long-$(1)-$(2)-install: long-$(1)-$(2)-build
	cd $(ARCH_DIR)/long/$(ODP_$(1)_BOARD_NAME)/$(ODP_$(1)_OS_NAME)/$(2) && $$(MAKE) -f $(TOP_DIR)/long/$(2)/Makefile install \
	LONT_DIR=$(LONT_DIR) ODP_TOOLCHAIN_DIR=$(K1ST_DIR)/ ODP_ARCH=$(1)

long-$(1)-$(2)-valid: long-$(1)-$(2)-build extra-install
	cd $(ARCH_DIR)/long/$(ODP_$(1)_BOARD_NAME)/$(ODP_$(1)_OS_NAME)/$(2) && $$(MAKE) -f $(TOP_DIR)/long/$(2)/Makefile valid \
	LONT_DIR=$(LONT_DIR) ODP_TOOLCHAIN_DIR=$(K1ST_DIR)/ ODP_ARCH=$(1)

long-$(1)-$(2)-build: long-$(1)-$(2)-configure
	mkdir -p $(ARCH_DIR)/long/$(ODP_$(1)_BOARD_NAME)/$(ODP_$(1)_OS_NAME)/$(2)
	cd $(ARCH_DIR)/long/$(ODP_$(1)_BOARD_NAME)/$(ODP_$(1)_OS_NAME)/$(2) && $$(MAKE) -f $(TOP_DIR)/long/$(2)/Makefile \
	LONT_DIR=$(LONT_DIR) ODP_TOOLCHAIN_DIR=$(K1ST_DIR)/ ODP_ARCH=$(1) all

long-$(1)-$(2)-clean:
	rm -Rf $(ARCH_DIR)/long/$(ODP_$(1)_BOARD_NAME)/$(ODP_$(1)_OS_NAME)/$(2)
endef

#
# Define rules for a given all long test for a given arch
#
define LONG_CONFIG_RULE
#$(1) CONFIG

long-$(1)-configure:
long-$(1)-install: $(foreach APP, $(LONGS), long-$(1)-$(APP)-install)
	( \
	  echo '# Generated test file'; \
	  echo 'SET(ENV{LD_LIBRARY_PATH} ../lib/host/:$$$$ENV{LD_LIBRARY_PATH})'; \
	  echo 'SET(ENV{PATH} ./:$$$$ENV{PATH})'; \
	  for test in $$$$(cd $(LONT_DIR)/$(ODP_$(1)_ARCH_BUILDDIR)/;\
	  	  	   	   find . -mindepth 2 -name CTestTestfile.cmake -exec dirname {} \;); do \
		  echo "SUBDIRS($$$$test)"; \
	  done \
	 ) > $(LONT_DIR)/$(ODP_$(1)_ARCH_BUILDDIR)/CTestTestfile.cmake

long-$(1)-valid: $(foreach APP, $(LONGS), long-$(1)-$(APP)-install) extra-install
	cd $(LONT_DIR)/$(ODP_$(1)_ARCH_BUILDDIR) && ctest -L $(if $(findstring simu,$(1)),sim,jtag)

long-$(1)-build: $(foreach APP, $(LONGS), long-$(1)-$(APP)-build)

long-$(1)-clean: $(foreach APP, $(LONGS), long-$(1)-$(APP)-clean)
	rm -Rf $(ARCH_DIR)/long/$(1)

$(foreach APP, $(LONGS), $(eval $(call LONG_RULE,$(1),$(APP))))
endef

#
# Global rules for parallel actions
#
define DO_RULES_PAR
# $(1) = RULE
$(1): $(foreach COMPONENT, $(COMPONENTS), $(COMPONENT)-$(1)) $($(1)_DEPS)
CHECK_LIST += $(foreach COMPONENT, $(COMPONENTS),$(COMPONENT)-$(1))
endef

#
# Global rules for sequential actions
#
define DO_RULES_SEQ
# $(1) = RULE
.PHONY: $(1)
$(1): $($(1)_DEPS)
	set -e; for rule in $(foreach COMPONENT, $(COMPONENTS), $(COMPONENT)-$(1)); do $$(MAKE) $$$${rule}; done

CHECK_LIST += $(foreach COMPONENT, $(COMPONENTS),$(COMPONENT)-$(1))
endef

# Thin Makefile wrapper around the CMake build.
#
# All build logic lives in CMakeLists.txt; this just maps the classic targets
# and option knobs onto CMake invocations.
#
# Options (override on the command line, e.g. `make TESTS=0`):
#   DISTRO_INSTALL=1   install shared/static libs, header and pkg-config (default 1)
#   TESTS=1            build the test suite (default 1)
#   EXAMPLES=1         build the example programs (default 1)
#
# Standard variables honored: CC, CFLAGS, DESTDIR, PREFIX, LIBDIR, INCLUDEDIR.

BUILD_DIR ?= build

DISTRO_INSTALL ?= 1
TESTS          ?= 1
EXAMPLES       ?= 1

onoff = $(if $(filter 1,$(1)),ON,OFF)

CMAKE_FLAGS := \
	-DDOTENV_DISTRO_INSTALL=$(call onoff,$(DISTRO_INSTALL)) \
	-DDOTENV_TESTS=$(call onoff,$(TESTS)) \
	-DDOTENV_EXAMPLES=$(call onoff,$(EXAMPLES))

ifdef PREFIX
    CMAKE_FLAGS += -DCMAKE_INSTALL_PREFIX=$(PREFIX)
endif
ifdef LIBDIR
    CMAKE_FLAGS += -DCMAKE_INSTALL_LIBDIR=$(LIBDIR)
endif
ifdef INCLUDEDIR
    CMAKE_FLAGS += -DCMAKE_INSTALL_INCLUDEDIR=$(INCLUDEDIR)
endif
# Only forward CC when the user actually set it (not make's built-in default).
ifneq ($(origin CC),default)
    CMAKE_FLAGS += -DCMAKE_C_COMPILER=$(CC)
endif
ifdef CFLAGS
    CMAKE_FLAGS += -DCMAKE_C_FLAGS=$(CFLAGS)
endif

.PHONY: all clean mrproper install test check examples configure dist

# Reconfigure on every build so option changes take effect; CMake makes this
# cheap and idempotent when nothing changed.
configure:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

all: configure
	cmake --build $(BUILD_DIR)

examples:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS) -DDOTENV_EXAMPLES=ON
	cmake --build $(BUILD_DIR) --target env_to_kvs

test check: configure
	cmake --build $(BUILD_DIR)
	ctest --test-dir $(BUILD_DIR) --output-on-failure

install: all
	DESTDIR=$(DESTDIR) cmake --install $(BUILD_DIR)

# Roll a versioned source tarball into $(BUILD_DIR)/dist/ (location comes from
# CPACK_PACKAGE_DIRECTORY in CMakeLists.txt).
dist: configure
	cpack --config $(BUILD_DIR)/CPackSourceConfig.cmake

mrproper clean:
	rm -rf $(BUILD_DIR)
	rm -f $(CURDIR)/dotenv-*.tar.gz   # sweep up any tarball from the old flat layout

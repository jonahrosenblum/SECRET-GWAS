include config.mk

# Based on https://hiltmon.com/blog/2013/07/03/a-simple-c-plus-plus-project-structure/
CC := g++
SRCDIR := src
BUILDDIR := build
SHAREDBUILDDIR := ../shared/build
TARGETDIR := bin
TESTDIR := tests
UNITTESTDIR := unit_tests
CONFIG:=dpi_config.json
DEMO_CONFIG:= dpi_config-demo.json

SRCEXT := cpp
# List of source files for your thread library
SOURCES = $(shell find $(SRCDIR) -type f -name *.cpp)
# Generate the names of the thread library's object files
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o))
OBJECTS += $(shell find $(SHAREDBUILDDIR) -type f -name *.o)

CFLAGS_TMP1=$(shell pkg-config oehost-$(CXX_COMPILER) --cflags)
CFLAGS_TMP2=${subst -mllvm -x86-speculative-load-hardening,, $(CFLAGS_TMP1)}
CFLAGS=${subst -fstack-protector-strong,, $(CFLAGS_TMP2)}
# CXXFLAGS=$(shell pkg-config oehost-$(CXX_COMPILER) --cflags)
# LDFLAGS=$(shell pkg-config oehost-$(CXX_COMPILER) --libs)

CFLAGS+= -O3 -g -Wall -std=c++14 -pthread
FSFLAGS := -ldl
INC := -I ../shared/include -I ../shared/third_party -I include
INC += ${LDFLAGS} ${INCDIR}
# INC += $(LDFLAGS) $(CRYPTO_LDFLAGS) $(INCDIR)
CRYPTFLAG := -lcryptopp
CURLFLAG := -lcurl
GLUEFLAGS := -L/opt/openenclave/share/pkgconfig/../../lib/openenclave/host -loehost -ldl -lpthread -lssl -lcrypto -lmbedcrypto -lmbedtls -lmbedx509# I have no idea why these flags fix the linking issue but they do. Someone who is better at Makefiles should fix this in the future :/

# list of test drivers (with main()) for development
TESTSOURCES := $(wildcard $(TESTDIR)/*test*.$(SRCEXT))
# names of test executables
TESTOBJECTS := $(patsubst $(TESTDIR)/%,%,$(TESTSOURCES:.$(SRCEXT)=))

ifeq ($(OS),Windows_NT)     # is Windows_NT on XP, 2000, 7, Vista, 10...
    detected_OS := Windows
else
    detected_OS := $(shell uname)
endif

# Use Mac library if on Mac
ifeq ($(detected_OS),Darwin)  # Important to not have a space after the comma
	LIBTHREAD = -lboost_thread-mt
	LIBCHRONO = -lboost_chrono-mt
else
	LIBTHREAD = -lboost_thread -lboost_system
	LIBCHRONO = -lboost_chrono
endif

all: shared dpi

# Generic rules for compiling a source file to an object file
${BUILDDIR}/%.o: ${SRCDIR}/%.${SRCEXT}
	@mkdir -p ${BUILDDIR}
	${CC} ${CFLAGS} ${INC} -c -o $@ $<

# Automatically generate any build rules for test*.cpp files
define make_tests
    $(1): $$(TESTDIR)/$(1).$$(SRCEXT)
	$$(CC) $$(CFLAGS) $$(INC) -o $$(TARGETDIR)/$(1).o $$^ $$(LIBTHREAD)
endef
$(foreach test,$(TESTOBJECTS),$(eval $(call make_tests,$(test))))

# Compile all test programs
tests: ${TESTOBJECTS}

shared:
	cd ../shared && $(MAKE) && cd ../dpi

# Compile the pager and tag this compilation
dpi: ${OBJECTS}
	@mkdir -p ${TARGETDIR}
	${CC} ${CFLAGS} ${INC} -o ${TARGETDIR}/$@ $^ ${LIBTHREAD} ${FSFLAGS} ${CRYPTFLAG} ${CURLFLAG} ${GLUEFLAGS}

run:
	./bin/dpi configs/$(CONFIG)

demo:
	./bin/dpi configs/$(DEMO-CONFIG)

clean:
	@echo "Cleaning..."
	rm -r ${BUILDDIR} ${TARGETDIR}

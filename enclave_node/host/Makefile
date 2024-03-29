SRCDIR := src
SHAREDBUILDDIR := ../../shared/build
BUILDDIR := build
TESTDIR := tests
include ../config.mk
####################################
EXECUTABLE = $(PROJECTNAME)host
CONFIG = configs/enclave_node_config.json

TESTSOURCES = $(wildcard $(TESTDIR)/test*.cpp)
# names of test executables
TESTS       = $(patsubst $(TESTDIR)/%,%,$(TESTSOURCES:.cpp=))
# List of source files for your thread library
SOURCES = $(shell find $(SRCDIR) -type f -name *.cpp)

# Generate the names of the thread library's object files
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.cpp=.o))
OBJECTS += $(shell find $(SHAREDBUILDDIR) -type f -name *.o)
DEBUGOBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.cpp=_debug.o))

NONOEOBJECTS = $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.cpp=_nonoe.o))
NONOEOBJECTS += $(shell find $(SHAREDBUILDDIR) -type f -name *.o)
INC := -I ../../shared/include -I ../../shared/third_party -I .. -pthread -I include -I .
CRYPTFLAG := -lcryptopp
CURLFLAG := -lcurl

######################################

# Use Mac library if on Mac
ifeq ($(detected_OS),Darwin)  # Important to not have a space after the comma
	LIBTHREAD = -lboost_thread-mt
	LIBCHRONO = -lboost_chrono-mt
else
	LIBTHREAD = -lboost_thread -lboost_system
	LIBCHRONO = -lboost_chrono
endif

CFLAGS=$(shell pkg-config oehost-$(C_COMPILER) --cflags)
CXXFLAGS=$(shell pkg-config oehost-$(CXX_COMPILER) --cflags)
# CXXFLAGS=${subst -mllvm -x86-speculative-load-hardening,, $(CXXFLAGS1)}
LDFLAGS=$(shell pkg-config oehost-$(C_COMPILER) --libs)
INCDIR=$(shell pkg-config oehost-$(C_COMPILER) --variable=includedir)

CFLAGS+= $(INC) 
CXXFLAGS+= -O3 $(INC)
LDFLAGS+= $(INC) $(LIBTHREAD)

TMP1=${subst -nostdinc,, $(CXXFLAGS)}
TMP2=${subst -I/opt/openenclave/share/pkgconfig/../../include/openenclave/3rdparty/libcxx,, $(TMP1)}
TMP3=${subst -I/opt/openenclave/share/pkgconfig/../../include/openenclave/3rdparty/libc,, $(TMP2)}
TMP4=${subst -I/opt/openenclave/share/pkgconfig/../../include/openenclave/3rdparty,, $(TMP3)}
TMP5=${subst -mllvm -x86-speculative-load-hardening,, $(TMP4)}
CXX_NONENC_FLAGS=${subst -I/opt/openenclave/share/pkgconfig/../../include,, $(TMP5)}
CXX_NONENC_FLAGS+= -lmbedtls -lmbedcrypto
# CXX_NONENC_FLAGS+= -mllvm -x86-speculative-load-hardening

all: build

pre:
	@ echo "Compilers used: $(CC), $(CXX)"
	oeedger8r ../$(PROJECTNAME).edl --untrusted \
		--search-path $(INCDIR) \
		--search-path $(INCDIR)/openenclave/edl/sgx
	@mkdir -p ${BUILDDIR}
	$(CC) -c $(CFLAGS) $(PROJECTNAME)_u.c -o $(BUILDDIR)/$(PROJECTNAME)_u.o

# Generic rules for compiling a source file to an object file
${BUILDDIR}/%.o: ${SRCDIR}/%.cpp
	${CXX} ${CXXFLAGS} -c -o $@ $<

build: pre $(OBJECTS)
	cd ../../shared && $(MAKE) && cd ../enclave_node/host
	$(CXX) -o $(EXECUTABLE) $(BUILDDIR)/$(PROJECTNAME)_u.o $(OBJECTS) $(LDFLAGS) ${CRYPTFLAG} ${CURLFLAG}

run: 
	./$(EXECUTABLE) $(CONFIG)

demo: 
	./$(EXECUTABLE) configs/enclave_node_config-demo.json

simulate:
	./$(EXECUTABLE) $(CONFIG) --simulate

# Automatically generate any build rules for test*.cpp files
define make_tests
	$(CXX) -c $(CXXFLAGS) -g $(TESTDIR)/$(1).cpp
	$(CXX) -g -o $(1) $(BUILDDIR)/$(PROJECTNAME)_u.o $(1).o $(LDFLAGS)
endef


test%: $(TESTDIR)/test%.cpp
	$(call make_tests,test$*)

	
tests: pre $(TESTS)

runtests: $(TESTS)
	$(foreach test,$(TESTS),./$(test) ../enclave/$(PROJECTNAME)enc.signed;)


# Generic rules for compiling a source file to an debug object file
${BUILDDIR}/%_debug.o: ${SRCDIR}/%.cpp
	@mkdir -p ${BUILDDIR}
	${CXX} ${CXXFLAGS} -g -c -o $@ $<

debugtests: $(TESTS)
	$(MAKE) -C .. debug
	$(foreach test,$(TESTS), $(DEBUGER) ./$(test) ../enclave/$(PROJECTNAME)enc_debug.signed --debug)

debug: pre $(DEBUGOBJECTS) 
	$(CC) -c -g $(CFLAGS) $(PROJECTNAME)_u.c -o $(BUILDDIR)/$(PROJECTNAME)_u_debug.o
	$(CXX) -g -o $(EXECUTABLE)_debug $(BUILDDIR)/$(PROJECTNAME)_u_debug.o $(DEBUGOBJECTS) $(LDFLAGS)

debugrun:
	$(DEBUGER) ./$(EXECUTABLE)_debug $(HOST_PORT) ../enclave/$(PROJECTNAME)enc_debug.signed --debug

clean:
	rm -rf ${BUILDDIR}
	rm -f $(PROJECTNAME)host $(PROJECTNAME)host.o $(PROJECTNAME)_u.o $(PROJECTNAME)_u.c $(PROJECTNAME)_u.h $(PROJECTNAME)_args.h $(PROJECTNAME).signed
	rm -f *.o $(TESTS) *.out *_debug test_*
	rm -f *_nonoe

# Generic rules for compiling a source file to an non oe object file
${BUILDDIR}/%_nonoe.o: ${SRCDIR}/%.cpp
	@mkdir -p ${BUILDDIR}
	${CXX} -DNON_OE ${CXX_NONENC_FLAGS} -c -o $@ $<


ENCLAVE_NONOEOBJECTS = $(wildcard ../enclave/build/*_nonoe.o)

nonoe: $(NONOEOBJECTS)
	$(CXX) $(CXX_NONENC_FLAGS) $(LIBTHREAD) $(LIBCHRONO) ${CRYPTFLAG} ${CURLFLAG} $(NONOEOBJECTS) $(ENCLAVE_NONOEOBJECTS) -o $(PROJECTNAME)_nonoe

run_nonoe: $(PROJECTNAME)_nonoe
	./$< $(CONFIG)

.PHONY: all build clean run debug simulate
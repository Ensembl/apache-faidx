
ifndef HTSLIB_DIR
  $(error HTSLIB_DIR is undefined, see README.txt for details)
endif

APR_CONFIG=$(shell which apr-config)

INCDIR=./include

LIB_OBJS = src/files_manager.o src/htslib_fetcher.o
MODULE_SRCS = src/mod_faidx.c src/htslib_fetcher.c src/files_manager.c

CC=gcc
CXX=g++
CFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(HTSLIB_DIR) -I$(INCDIR) -Wall
CXXFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(HTSLIB_DIR) -I$(INCDIR) -Wall
LDLIBS=-lhts -lz -lcrypto

DEPS = $(wildcard $INCDIR/*.h)

%.o: %.c $(DEPS) %.h
	$(CC) -fPIC -L$(HTSLIB_DIR) $(CFLAGS) -Wl,-rpath=$(HTSLIB_DIR) -c -o $@ $<

all:
	@echo There is no default make target.
	@echo Available make targets: apmodule, apmodule_debug, config_builder, lib, test

apmodule:
	apxs2 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -I$(INCDIR) -Wl,-rpath=$(HTSLIB_DIR) $(LDLIBS) $(MODULE_SRCS)

apmodule_debug:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -I$(INCDIR) -Wl,-rpath=$(HTSLIB_DIR) $(LDLIBS) $(MODULE_SRCS)

apmodule_coveralls:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -I$(INCDIR) -Wl,-rpath=$(HTSLIB_DIR) "-Wc,-g -O0 --coverage" $(LDLIBS) -lgcov $(MODULE_SRCS)

config_builder: $(DEPS) lib
	cd config_builder && $(MAKE) config_builder

install: apmodule
	apxs2 -i -n faidx src/.libs/mod_faidx.so

lib: $(MODULE_SRCS)
	cd src && $(MAKE) lib

test: check
check: $(DEPS) lib
	cd test && $(MAKE) test

.PHONY: config_builder check

clean:
	rm -rf *.o *.so *.lo *.slo *.la *.a .libs
	cd test && $(MAKE) clean
	cd src && $(MAKE) clean
	cd config_builder && $(MAKE) clean

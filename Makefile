
APR_CONFIG=$(shell which apr-config)

INCDIR=./include

TARGET_LIB = librefseq.a
LIB_OBJS = files_manager.o htslib_fetcher.o

CC=gcc
CXX=g++
CFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(HTSLIB_DIR) -I$(INCDIR) -Wall
CXXFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(HTSLIB_DIR) -I$(INCDIR) -Wall
LDLIBS=-lhts -lz -lcrypto

DEPS = $(wildcard $INCDIR/*.h) $(TARGET_LIB)

%.o: %.c $(DEPS) %.h
	$(CC) -fPIC -L$(HTSLIB_DIR) $(CFLAGS) -Wl,-rpath=$(HTSLIB_DIR) -c -o $@ $<

all:

apmodule:
	apxs2 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -I$(INCDIR) -Wl,-rpath=$(HTSLIB_DIR) $(LDLIBS) mod_faidx.c htslib_fetcher.c files_manager.c

apmodule_debug:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -I$(INCDIR) -Wl,-rpath=$(HTSLIB_DIR) $(LDLIBS) mod_faidx.c htslib_fetcher.c files_manager.c

apmodule_coveralls:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -I$(INCDIR) -Wl,-rpath=$(HTSLIB_DIR) "-Wc,-g -O0 --coverage" $(LDLIBS) -lgcov mod_faidx.c htslib_fetcher.c files_manager.c

config_builder: $(DEPS)
	cd config_builder && $(MAKE) config_builder

install: apmodule
	apxs2 -i -n faidx .libs/mod_faidx.so

lib: $(TARGET_LIB)

$(TARGET_LIB): $(LIB_OBJS)
	ar rcs $@ $^
	ranlib $@

test: check
check: $(DEPS)
	cd test && $(MAKE) test

.PHONY: config_builder check

clean:
	rm -rf *.o *.so *.lo *.slo *.la *.a .libs
	cd test && $(MAKE) clean
	cd config_builder && $(MAKE) clean

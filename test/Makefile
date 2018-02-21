
APR_CONFIG=$(shell which apr-config)

INCDIR=../include
REFSEQ_LIB=../librefseq.a

TARGETS = files_manager_t htslib_fetcher_t
MAKEFILE_PATH=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))

CC=gcc
CXX=g++
CFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(INCDIR) -Wall -I$(HTSLIB_DIR) -g
CXXFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(INCDIR) -Wall -I$(HTSLIB_DIR)

LDFLAGS=$(shell ${APR_CONFIG} --ldflags)
LDLIBS=-L$(HTSLIB_DIR) -lhts -lz $(shell ${APR_CONFIG} --libs --link-ld) -lcrypto

DEPS = $(wildcard $INCDIR/*.h) $(REFSEQ_LIB) test_harness.o

all: $(TARGETS)
test: check
check: $(TARGETS)

$(TARGETS) : % : %.o $(DEPS)
	@echo
	@echo Testing $@
	gcc $@.o test_harness.o $(REFSEQ_LIB) $(LDFLAGS) $(LDLIBS) -Wl,-rpath=$(HTSLIB_DIR) -o $@
	./$@

%.o: %.c
	gcc -fPIC -DDATAFILE_PATH="$(MAKEFILE_PATH)../" $(CFLAGS) -Wl,-rpath=$(HTSLIB_DIR) -g -c -o $@ $<

#files_manager: $(FILES_MGR_OBJ)
#	gcc $^ -L/home/lairdm/src/htslib -I/home/lairdm/src/htslib files_manager.o -lhts -lz $(LDFLAGS) $(LDLIBS) -Wl,-rpath=/home/lairdm/src/htslib -o $@

clean:
	rm -rf *.o *.so *.lo *.slo *.la .libs
	$(shell for target in $(TARGETS); do rm $$target; done)

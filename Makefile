
APR_CONFIG=$(shell which apr-config)

INCDIR=./include

TARGET_LIB = librefseq.a
LIB_OBJS = files_manager.o htslib_fetcher.o

CC=gcc
CXX=g++
CFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(HTSLIB_DIR) -I$(INCDIR) -Wall
CXXFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(HTSLIB_DIR) -I$(INCDIR) -Wall

DEPS = $(wildcard $INCDIR/*.h) $(TARGET_LIB)

%.o: %.c $(DEPS) %.h
	$(CC) -fPIC -L$(HTSLIB_DIR) $(CFLAGS) -Wl,-rpath=$(HTSLIB_DIR) -c -o $@ $<
	$(CC) -fPIC -DDEFAULT_FILES_CACHE_SIZE=1 -L$(HTSLIB_DIR) $(CFLAGS) -Wl,-rpath=$(HTSLIB_DIR) -g -c -o test/$@ $<

all:

apmodule:
	apxs2 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -lhts -lz mod_faidx.c htslib_fetcher.c

apmodule_debug:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -lhts -lz mod_faidx.c htslib_fetcher.c

apmodule_coveralls:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) "-Wc,-g -O0 --coverage" -lhts -lz -lgcov mod_faidx.c htslib_fetcher.c

install: apmodule
	apxs2 -i -n faidx .libs/mod_faidx.so

lib: $(TARGET_LIB)

$(TARGET_LIB): $(LIB_OBJS)
	ar rcs $@ $^
	ranlib $@

test: check
check: $(DEPS)
	cd test && $(MAKE) test

OBJS = files_manager.o htslib_fetcher.o
DRIVER_OBJ = fetcher_driver.o htslib_fetcher.o
DRIVER_ITER_OBJ = fetcher_iter_driver.o htslib_fetcher.o
DEPS = htslib_fetcher.h
DRIVER_C = htslib_fetcher.c fetcher_driver.c

fetcher_driver: $(DRIVER_OBJ)
	gcc -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -o $@ $^ -lhts -lz 

fetcher_iter_driver: $(DRIVER_ITER_OBJ)
	gcc -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -o $@ $^ -lhts -lz 

fetcher_test: $(DRIVER_C)
	gcc -L/home/lairdm/src/htslib -I/home/lairdm/src/htslib -Wl,-rpath=/home/lairdm/src/htslib -o $@ $^ -lhts -lz

clean:
	rm -rf *.o *.so *.lo *.slo *.la .libs
	cd test && $(MAKE) clean

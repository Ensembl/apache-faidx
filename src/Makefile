
APR_CONFIG=$(shell which apr-config)

INCDIR=../include

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

lib: $(TARGET_LIB) $(DEPS)

$(TARGET_LIB): $(LIB_OBJS)
	ar rcs $@ $^
	ranlib $@

all: lib

clean:
	rm -rf *.o *.so *.lo *.slo *.la *.a .libs


APR_CONFIG=$(shell which apr-config)

INCDIR=../include
REFSEQ_LIB=../librefseq.a

TARGET=config_builder

MAKEFILE_PATH=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))

CC=gcc
CXX=g++
CFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(INCDIR) -Wall -I$(HTSLIB_DIR) -g
CXXFLAGS=$(shell ${APR_CONFIG} --cflags --cppflags --includes) -I$(INCDIR) -Wall -I$(HTSLIB_DIR)

LDFLAGS=$(shell ${APR_CONFIG} --ldflags)
LDLIBS=-L$(HTSLIB_DIR) -lhts -lz $(shell ${APR_CONFIG} --libs --link-ld) -lcrypto

DEPS = $(wildcard $INCDIR/*.h) $(REFSEQ_LIB)

all: $(TARGET)

config_builder: $(TARGET).o $(DEPS)

%.o: %.c
	gcc -fPIC -DDATAFILE_PATH="$(MAKEFILE_PATH)../" $(CFLAGS) -Wl,-rpath=$(HTSLIB_DIR) -g -c -o $@ $<

clean:
	rm -rf *.o config_builder .libs

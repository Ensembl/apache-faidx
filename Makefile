all:
	apxs2 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -lhts -lz mod_faidx.c

debug:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -lhts -lz mod_faidx.c

coveralls:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) "-Wc,-g -O0 --coverage" -lhts -lz -lgcov mod_faidx.c

install:
	apxs2 -i -n faidx .libs/mod_faidx.so

clean:
	rm -rf *.o *.so *.lo *.slo *.la .libs

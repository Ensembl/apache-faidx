all:
	apxs2 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -lhts -lz mod_faidx.c htslib_fetcher.c

debug:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -lhts -lz mod_faidx.c htslib_fetcher.c

coveralls:
	apxs2 -DDEBUG=1 -c -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) "-Wc,-g -O0 --coverage" -lhts -lz -lgcov mod_faidx.c htslib_fetcher.c

install:
	apxs2 -i -n faidx .libs/mod_faidx.so

DRIVER_OBJ = fetcher_driver.o htslib_fetcher.o
DRIVER_ITER_OBJ = fetcher_driver2.o htslib_fetcher.o
DEPS = htslib_fetcher.h
DRIVER_C = htslib_fetcher.c fetcher_driver.c

%.o: %.c $(DEPS)
	gcc -fPIC -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -c -o $@ $<

fetcher_driver: $(DRIVER_OBJ)
	gcc -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -o $@ $^ -lhts -lz 

fetcher_iter_driver: $(DRIVER_ITER_OBJ)
	gcc -L$(HTSLIB_DIR) -I$(HTSLIB_DIR) -Wl,-rpath=$(HTSLIB_DIR) -o $@ $^ -lhts -lz 

fetcher_test: $(DRIVER_C)
	gcc -L/home/lairdm/src/htslib -I/home/lairdm/src/htslib -Wl,-rpath=/home/lairdm/src/htslib -o $@ $^ -lhts -lz

clean:
	rm -rf *.o *.so *.lo *.slo *.la .libs

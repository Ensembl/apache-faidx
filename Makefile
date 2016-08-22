all:
	apxs2 -c -L/home/lairdm/src/htslib/ -I/home/lairdm/src/htslib/ -Wl,-rpath=/home/lairdm/src/htslib/ -lhts -lz mod_faidx.c

debug:
	apxs2 -DDEBUG=1 -c -L/home/lairdm/src/htslib/ -I/home/lairdm/src/htslib/ -Wl,-rpath=/home/lairdm/src/htslib/ -lhts -lz mod_faidx.c

clean:
	rm -rf *.o *.so *.lo *.slo *.la .libs

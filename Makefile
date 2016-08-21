all:
	apxs2 -c -L/home/lairdm/tmp/htslib/ -I/home/lairdm/tmp/htslib/ -Wl,-rpath=/home/lairdm/tmp/htslib/ -lhts -lz mod_faidx.c

debug:
	apxs2 -DDEBUG=1 -c -L/home/lairdm/tmp/htslib/ -I/home/lairdm/tmp/htslib/ -Wl,-rpath=/home/lairdm/tmp/htslib/ -lhts -lz mod_faidx.c

clean:
	rm -rf *.o *.so *.lo *.slo *.la .libs

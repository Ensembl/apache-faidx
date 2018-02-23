# apache-faidx

## Installation

You *must* use the prefork worker module as htslib is not thread safe. Workers such as MPM will create unpredictable results.

## Apache directives

```
LoadModule faidx_module /path/to/mod_faidx.so

# The maximum number of backing (fasta) files to keep open at once
sequence_cachesize 100

<SeqFile /faidx/files/Homo_sapiens.GRCh38.dna.toplevel.fa.gz>
  Seq 1 md5 FFFFFFFF
  Seq 2 md5 EEEEEEEE
  Alias 1 1
  Alias 1 chr1
</SeqFile>

<SeqFile /faidx/files/Felis_catus.Felis_catus_6.2.dna.fa>
  Seq 1 md5 CCCCCCCC
  Seq 2 sha256 BB61EF40814CE34C1EDF0EDB609854BE9793198A8F60B67D9FB26643C32281D3
  Alias 1 chr1
</SeqFile>

<Location /faidx>
        SetHandler faidx
</Location>

```

## Example curl command

```
curl "http://localhost/faidx/metadata/FFFFFFFF"
#curl -v -N -H 'Content-type: application/json' -X POST -d '{"location": ["1:1000-2000", "9:300000-305000"], "set": "GRCh38"}' "http://localhost/faidx/region/GRCh38"
#curl "http://localhost/faidx/region/GRCh38?location=9%3A10000-15000&location=X%3A10000-11000"
curl -v -N -H "Accept: text/x-fasta" "http://localhost/faidx/FFFFFFFF?range=2000-3000,11000-12000"
curl -v -N -H "Accept: text/x-fasta" "http://localhost/faidx/md5/FFFFFFFF?range=2000-3000,11000-12000&strand=-1&translate=1"
curl -v -N -H "Accept: text/x-fasta" "http://localhost/faidx/FFFFFFFF"

curl "http://localhost/faidx/FFFFFFFF?rangen=12:43768112-43768272,43771220-43771365,43772180-43772362,43772912-43773042,43773045-43773072,43773965-43773971" (IRAK4, ENST00000448290.6)
curl "http://localhost/faidx/FFFFFFFF?range=12:43768112-43768272,43771220-43771365,43772180-43772362,43772912-43773042,43773045-43773072,43773965-43773971&translate=1" (IRAK4, ENSP00000390651.3)
curl -H "Accept: text/x-fasta" http://localhost/faidx/FFFFFFFF
```

## TODO

* Chunked return type or content-length depending on return size, not implemented yet, it seems to automatically do chunked, but perhaps set content-length manually if we know it'll be smaller
* Add support for aliases not associated with checksums, ie 1 -> file:sequence, have to allow a NULL for checksum_obj_ptr in alias_obj
* Labels (/md5/<checksum>/) not implemented, and the ap_set_flag_slot causes a segfault if used, might have to implement as a function call type directive

[![Build Status](https://travis-ci.org/lairdm/apache-faidx.svg?branch=master)](https://travis-ci.org/lairdm/apache-faidx) [![Coverage Status](https://coveralls.io/repos/github/lairdm/apache-faidx/badge.svg?branch=master)](https://coveralls.io/github/lairdm/apache-faidx?branch=master)
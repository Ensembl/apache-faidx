# apache-faidx

## Apache directives

```
LoadModule faidx_module /path/to/mod_faidx.so

FaidxSet human /faidx/files/Homo_sapiens.GRCh38.dna.toplevel.fa.gz
FaidxSet cat /faidx/files/Felis_catus.Felis_catus_6.2.dna.fa

<Location /faidx>
        SetHandler faidx
</Location>

```

## Example curl command

```
curl "http://localhost/faidx?set=human&location=9%3A10%2C000-15%2C000"
curl -H "Content-type: text/x-fasta" "http://localhost/faidx?set=human&location=9%3A10%2C000-15%2C000"
curl -H 'Content-type: application/json' -H 'Accept: application/json' -X POST -d '{"location": ["1:1000-2000", "9:300000-305000"], "set": "human"}' http://localhost/faidx/
curl "http://localhost/faidx/locations?set=human&location=9%3A10000-15000&location=X%3A10000-11000
curl "http://localhost/faidx/sets"
curl "http://localhost/faidx/locations/human/"
```

[![Build Status](https://travis-ci.org/lairdm/apache-faidx.svg?branch=master)](https://travis-ci.org/lairdm/apache-faidx) [![Coverage Status](https://coveralls.io/repos/github/lairdm/apache-faidx/badge.svg?branch=master)](https://coveralls.io/github/lairdm/apache-faidx?branch=master)
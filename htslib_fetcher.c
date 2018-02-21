/* htslib seq fetch and assembly module

 Wrapper between mod_faidx and htslib to fetch one or
 more sequences and either return that or translate
 to a protein sequence.

 Copyright [2016-2017] EMBL-European Bioinformatics Institute
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

// String format: Y:1-100,200-300,600-700:1

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "htslib_fetcher.h"
#include <stdio.h>

const char* codons[5][5] = {
 //   AA        AC        AG        AT        AX
 //   00        01        02        03        04
  { (char []){"KNKNX"}, (char []){"TTTTX"}, (char []){"RSRSX"}, (char []){"IIMIX"}, (char []){"XXXXX"} },
 //   CA         CC         CG         CT         CX
 //   10         11         12         13         14
  { (char []){"QHQHX"}, (char []){"PPPPX"}, (char []){"RRRRX"}, (char []){"LLLLX"}, (char []){"XXXXX"} },
 //  GA          GC         GG         GT         GX
 //  20          21         22         23         24
  { (char []){"EDEDX"}, (char []){"AAAAX"}, (char []){"GGGGX"}, (char []){"VVVVX"}, (char []){"XXXXX"} },
 //  TA          TC         TG         TT         TX
 //  30          31         32         33         34
  { (char []){"*Y*YX"}, (char []){"SSSSX"}, (char []){"*CWCX"}, (char []){"LFLFX"}, (char []){"XXXXX"} },
 //   XA         XC         XG         XT         XX
 //   40         41         42         43         44
  { (char []){"XXXXX"}, (char []){"XXXXX"}, (char []){"XXXXX"}, (char []){"XXXXX"}, (char []){"XXXXX"} }
};

const char* revcom = "TGCAN";

/* table to convert character of base to translation array element value */
static int trnconv[] =
{
    /* characters less than 64 */
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /*' '  !   "   #   $   %   &   '   (   )   *   +   ,   -   .   / */
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /* 0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ? */
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /* @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O */
     4,  0,  4,  1,  4,  4,  4,  2,  4,  4,  4,  4,  4,  4,  4,  4,

  /* P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _ */
     4,  4,  4,  4,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /* `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o */
     4,  0,  4,  1,  4,  4,  4,  2,  4,  4,  4,  4,  4,  4,  4,  4,

  /* p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~   del */
     4,  4,  4,  4,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4
};


/*
 ** table to convert character of COMPLEMENT of base to translation array
 ** element value
 */

static int trncomp[] =
{
    /* characters less than 64 */
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /*' '  !   "   #   $   %   &   '   (   )   *   +   ,   -   .   / */
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /* 0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ? */
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /* @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O*/
     4,  3,  4,  2,  4,  4,  4,  1,  4,  4,  4,  4,  4,  4,  4,  4,

  /* P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _ */
     4,  4,  4,  4,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,

  /* `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o */
     4,  3,  4,  2,  4,  4,  4,  1,  4,  4,  4,  4,  4,  4,  4,  4,

  /* p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~   del */
     4,  4,  4,  4,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4
};


char* tark_fetch_seq(faidx_t* fai, const char *str, int *seq_len) {
  char** seqs;
  char* s = NULL;
  int nseqs, strand, k, l;
  int i = 0;

  seqs = tark_fetch_seqs(fai, str, seq_len, &nseqs, &strand);

  if(seqs != NULL) {

    s = (char*)malloc((*seq_len+1) * sizeof(char));
    for(k=0; k<nseqs; k++) { // build the final sequence
      l = strlen(seqs[k]);
      if(l > 0) {
	strncpy(s+i, seqs[k], l);
	i += l;
	free(seqs[k]);
      }
    }
    s[i] = 0;
    free(seqs);

    if(strand == -1) {
      puts("reversing");
      tark_revcomp_seq(s);      
    }

  }

  return s;
}

char* tark_translate_seq(faidx_t* fai, const char *str, int *seq_len) {
  char **seqs;
  char* s = NULL;
  int nseqs, strand, k;

  seqs = tark_fetch_seqs(fai, str, seq_len, &nseqs, &strand);

  if(seqs != NULL) {
    s = tark_translate_seqs(seqs, *seq_len, nseqs, strand);
    for(k=0; k<nseqs; k++) {
      if(seqs[k] != NULL) {
	free(seqs[k]);
      }
    }
    free(seqs);
  }

  return s;
    
}

char* tark_iterator_fetch_translated_seq(seq_iterator_t* siterator, int *seq_len, char* seq_ptr) {
  int r;
  int i;
  int k = 0;
  int fetch_len = *seq_len;
  int bytes_to_cr = -1;
  //  int iterated_length;
  //  int translated_length;
  char* seq;
  char* translated_seq;

  //  translated_length = tark_iterator_translated_length(siterator, &r, NULL);
  //  iterated_length = translated_length - r;

  if(r <= 0) {
    *seq_len = 0;
    return NULL;
  }

  /* Do we have a line length? */
  if(siterator->line_length) {
    fetch_len = tark_iterator_adjusted_seq_len(*seq_len, r, siterator->seq_iterated, siterator->line_length, &bytes_to_cr);
  } else {
    fetch_len = r < *seq_len ? r : *seq_len;
  }

  /* Is remaining translated sequence less than what we're told
     our full window would use with CR added? If yes, then only
     fetch the remaining abount of sequence, the CR calculations
     should still work out fine. */
  //  if(r < fetch_len) {
  //    fetch_len = r;
  //  }

  // Reuse r
  r = fetch_len * 3;
  seq = _tark_iterator_fetch_seq(siterator, &r, NULL, 0);

  if(seq_ptr == NULL) {
    translated_seq = malloc((*seq_len) + 1);
  } else {
    translated_seq = seq_ptr;
  }
  translated_seq[(*seq_len)] = '\0';

  for(i = 2; i < r; i+=3) { // Work our way through the current row
    //    printf("i: %d\n", i);
    if(bytes_to_cr == 0) {
      translated_seq[k] = '\n';
      k++;
      bytes_to_cr = siterator->line_length;
    }
    translated_seq[k] = codons[trnconv[(int)seq[i-2]]]
                              [trnconv[(int)seq[i-1]]]
                              [trnconv[(int)seq[i]]];
    //    printf("codon: %c%c%c, p: %c\n", seq[i-2], seq[i-1], seq[i], translated_seq[k]);
    k++;
    bytes_to_cr--;
  }

  if(r == i) { // we have 1 leftover bp
    //    puts("2 left");
    translated_seq[k] = codons[trnconv[(int)seq[i-2]]]
                              [trnconv[(int)seq[i-1]]]
                              [4];
    //    printf("codon: %c%c%c, p: %c\n", seq[i-2], seq[i-1], 'N', translated_seq[k]);
    k++;

  } else if(++r == i) { // we have 2 leftover bp
    //    puts("1 left");
    translated_seq[k] = codons[trnconv[(int)seq[i-2]]]
                              [4]
                              [4];
    //    printf("codon: %c%c%c, p: %c\n", seq[i-2], 'N', 'N', translated_seq[k]);
    k++;
  }

  translated_seq[k] = '\0';
  *seq_len = k;
  free(seq);

  return translated_seq;

}

/* We need to determine how many bytes to print before we add our first
   carriage return. Then how many bytes to retrieve in total, taking in
   to account all the CR that will be mixed in.

   Given the iterator and remaining window available, reset the window
   parameter to the bytes to retrieve from the faidx and return the
   bytes to write before we should print our first CR.

   This is unix type OS specific! It will not work in DOS/Windows based
   operating systems (LF vs CR + LF)
*/

int tark_iterator_adjusted_seq_len(int window, int bp_remaining, int bp_iterated, int line_length, int* bytes_to_cr) {
  int bp_possible;
  int window_remaining;
  int cr_count;
  int possible_lines;
  int bp_in_lines;
  int bp_with_crs;
  static int start_window_with_cr = 0;

  /* How many bytes could be possibly send? */
  bp_possible = bp_remaining < window ? bp_remaining : window;

  /* Don't be silly, why are you asking for the bytes to retrieve
     when there's no line length set? */
  if(line_length == 0) {
    *bytes_to_cr = -1;
    return bp_possible;
  }

  /* How many bytes before the first CR needs to be sent */
  if(start_window_with_cr) {
    start_window_with_cr = 0;
    *bytes_to_cr = 0;
  } else {
    *bytes_to_cr = line_length - (bp_iterated % line_length);
  }

  /* If we have less bytes to send than until the next end of line,
     tell the caller to just fetch that many */
  if(*bytes_to_cr > bp_possible) {
    return bp_possible;
  }

  cr_count = 1;

  /* We know we have at least to the next end of line worth of
     bytes to send */
  window_remaining = window - (*bytes_to_cr + 1);

  /* How many lines could we send after the first cr? */
  possible_lines = window_remaining / (line_length + 1);

  /* How many bp does this represent? */
  bp_in_lines = possible_lines * line_length;

  cr_count += possible_lines;

  /* The last training bit after the last end of line */
  window_remaining -= (bp_in_lines + possible_lines);

  /* If the window remaining had an exact number of lines, we're
     at the end of a line and don't add that remaining CR*/
  if(window_remaining == line_length) {
    cr_count--;
        start_window_with_cr = 1;
  }

  /* In a theoretical full window, how many total bp would we
     want to send? */
  bp_with_crs = *bytes_to_cr + bp_in_lines + window_remaining;

  /* If the remaining bp is less than what we'd send in the window,
     tell the caller to fetch that. Otherwise we want to suck the full
     amount that would fit in our window */
  return bp_possible < bp_with_crs ? bp_possible : bp_with_crs;
}

char* tark_iterator_fetch_seq(seq_iterator_t* siterator, int *seq_len, char* seq_ptr) {
  return _tark_iterator_fetch_seq(siterator, seq_len, seq_ptr, siterator->line_length);
}

char* _tark_iterator_fetch_seq(seq_iterator_t* siterator, int *seq_len, char* seq_ptr, int do_line_length) {
  char* s = NULL;
  char* seg_seq = NULL;
  int bp_retrieved = 0;
  int cr = 0;
  int len, seg_start, seg_end, segment_remaining, fetch_len;
  int bytes_to_cr = -1;
  int bp_remaining = siterator->seq_length - siterator->seq_iterated;
  seq_location_t *segment = NULL;

  //  *seq_len = *seq_len > bp_remaining ? bp_remaining : *seq_len;

  if(bp_remaining <= 0) {
    *seq_len = 0;
    return NULL;
  }

  /* We allow the user to send us a pointer to a string they want
     us to fill in, rather than allocating our own */
  if(seq_ptr == NULL) {
    //s = calloc(*seq_len + 1, 1);
    s = malloc(*seq_len + 1);
    if(s == NULL) { // If we aren't able to allocate the memory, bail.
      *seq_len = 0;
      return NULL;
    }
  } else {
    s = seq_ptr;
  }

  s[*seq_len] = 0;

  if(do_line_length) {
    fetch_len = tark_iterator_adjusted_seq_len(*seq_len, bp_remaining, siterator->seq_iterated, siterator->line_length, &bytes_to_cr);
  } else {
    fetch_len = *seq_len > bp_remaining ? bp_remaining : *seq_len;
  }

  // If we're on the reverse strand we need to seek the interator,
  // otherwise our seeking should be in the correct place as we retrieve
  if( siterator->strand == -1 &&
      ! tark_iterator_seek( siterator,
			    ( bp_remaining - fetch_len ) ) ) {

    /* If we weren't given a pointer by the caller,
       the memory isn't ours to free */
    if(seq_ptr == NULL) {
      free(s);
    }
    return NULL;
  }

  /* Loop through fetching segments until we reach our limit */
  while(bp_retrieved < fetch_len) {
    segment = &(siterator->locations[siterator->segment_ptr]);
    segment_remaining = segment->length - siterator->segment_bp_ptr;
    bp_remaining = fetch_len - bp_retrieved;
    seg_start = segment->start + siterator->segment_bp_ptr;

    if(segment_remaining > bp_remaining) {
      // We only want part of this segment
      seg_end = segment->start + siterator->segment_bp_ptr + bp_remaining - 1;
      siterator->segment_bp_ptr += bp_remaining;
    } else {
      // We want all of the segment, plus move to next segment
      seg_end = segment->end;
      siterator->segment_bp_ptr = 0;
      siterator->segment_ptr++;
    }

    seg_seq = faidx_fetch_seq(siterator->fai,
			      siterator->seq_name, 
			      seg_start,
			      seg_end,
			      &len);

    /* If we have a line length, use our custom memcpy wrapper
       that will handle adding the CR as needed */
    if(do_line_length) {
      cr += memcpy_with_cr(s+bp_retrieved+cr, seg_seq, len, siterator->line_length, &bytes_to_cr);
      bp_retrieved += len;

      /* Very specific case where we need to pad out one last EOL */
      if(bytes_to_cr == 0 && (bp_retrieved+cr+1) == *seq_len) {
	*(char*)(s+bp_retrieved+cr) = '\n';
	cr++;
      }
    } else {
      memcpy(s+bp_retrieved, seg_seq, len);
      bp_retrieved += len;
    }
    free(seg_seq);
  }

  siterator->seq_iterated += bp_retrieved;
  *seq_len = bp_retrieved+cr;
  s[*seq_len] = 0;

  if(siterator->strand == -1) {
    //    puts("reversing");
    tark_revcomp_seq(s);      
  }

  return s;

}

/* We're going to be a little unsafe with our memory copying here because the code that
   calls us should be sane in how it passes parameters. We're not going to recheck thing
   that the caller shouldn't have gotten wrong. */

int memcpy_with_cr(void* dest, void* src, int len, int line_len, int *bytes_to_cr) {
  int cr = 0;

  if(len < *bytes_to_cr) {
    memcpy(dest, src, len);
    *bytes_to_cr -= len;
    return 0;
  }

  /* We have to inject a CR before we start looping on line lengths */
  if(*bytes_to_cr > 0) {
    memcpy(dest, src, *bytes_to_cr);
    dest += *bytes_to_cr;
    src += *bytes_to_cr;
    *(char*)dest = '\n';
    cr++;
    len -= *bytes_to_cr;
  } else if(*bytes_to_cr == 0) {
    *(char*)dest = '\n';
    cr++;
  }

  while(len > line_len) {
    memcpy(dest+cr, src, line_len);
    dest += line_len;
    src += line_len;
    *(char*)(dest+cr) = '\n';
    cr++;
    len -= line_len;
  }
   
  memcpy(dest+cr, src, len);
  *bytes_to_cr = line_len - len;

  return cr;
}

void tark_iterator_set_line_length(seq_iterator_t* siterator, unsigned int length) {
  if(siterator == NULL) {
    return;
  }

  siterator->line_length = length;
}

int tark_iterator_seek(seq_iterator_t* siterator, unsigned int bp) {
  int bp_count = 0;
  int i;

  if(bp > siterator->seq_length) {
    return 0; // error, too far
  }

  for(i = 0; bp_count <= bp; i++) {
    if((bp_count + siterator->locations[i].length) < bp) {
      bp_count += siterator->locations[i].length;
      continue;
    }

    siterator->segment_ptr = i;
    siterator->segment_bp_ptr = bp - bp_count;

    return 1;
  }

  return 0; // something is very wrong

}

int tark_iterator_locations_count(seq_iterator_t* siterator) {
  int bp_counted = 0;
  int i;

  /* Sanity checking */
  if(siterator->locations == NULL) {
    return 0;
  }

  for(i = 0; bp_counted < siterator->seq_length; i++) {
    bp_counted += siterator->locations[i].length;
  }

  return i;
}

char* tark_revcomp_seq(char *seq) {
  char tmp;
  int i, k, l, l2;

  l = strlen(seq);
  l2 = l/2;

  // Two counters, less maths than doing a subtraction each time
  k = l - 1;
  for(i = 0; i < l2; i++) {
    /* Keep any CR in place when reversing */
    if(seq[k] == '\n') { k--; }
    if(seq[i] == '\n') { i++; }
    tmp = revcom[ trnconv[(int)seq[k]] ];
    seq[k] = revcom[ trnconv[(int)seq[i]] ];
    seq[i] = tmp;

    k--;
  } 

  if(l % 2 != 0) {
    seq[i] = revcom[ trnconv[(int)seq[i]] ];
  }

  return seq;
}

char* tark_rev_seq(char* seq) {
  char tmp;
  int i, k, l, l2;

  l = strlen(seq);
  l2 = l/2;

  // Two counters, less maths than doing a subtraction each time
  k = l - 1;
  for(i = 0; i < l2; i++) {
    tmp = seq[k];
    seq[k] = seq[i];
    seq[i] = tmp;

    k--;
  } 

  return seq;  
}

char* tark_translate_seqs(char **seqs, int seq_len, int nseqs, int strand) {
  int i, k, l, r;
  int lenmod3;
  char* seq;
  int *trntbl;

  // Allocate a sufficient sized string
  seq = malloc(((seq_len / 3) + 2) * sizeof(char));

  //  printf("nseqs: %d\n", nseqs);
  i = k = 0; nseqs--;

  // Handle strand, change the translation table being used
  if(strand == -1) {
    trntbl = trncomp;
    i = seq_len % 3;
    if(i) { // If we're starting with an initial phase, put that trailing X on the front
      seq[k] = 'X';
      k++;
    }
  } else {
    trntbl = trnconv;
  }

  for(r = 0; ; r++) { // For each row in the set of sequences
    l = strlen(seqs[r]);
    lenmod3 = l - ((l-i) % 3);

    //    printf("l: %d, lenmod3: %d\n", l, lenmod3);

    for(; i < lenmod3; i+=3) { // Work our way through the current row
      //    printf("i: %d k: %d\n", i, k);
    seq[k] = codons[trntbl[(int)seqs[r][i]]]
                   [trntbl[(int)seqs[r][i+1]]]
                   [trntbl[(int)seqs[r][i+2]]];
    //      printf("codon: %c%c%c, p: %c\n", seqs[r][i], seqs[r][i+1], seqs[r][i+2], seq[k]);
      k++;
    }

    //    printf("out. i: %d k: %d\n", i, k);
    if(r >= nseqs) break;

    // if we have leftover bp
    if(--l == i) { // we have 1 leftover bp
      puts("1 left");
      seq[k] = codons[trntbl[(int)seqs[r][i]]]
                     [trntbl[(int)seqs[r+1][0]]]
                     [trntbl[(int)seqs[r+1][1]]];
      
      //      printf("codon: %c,%c,%c, p: %c\n", seqs[r][i], seqs[r+1][0], seqs[r+1][1], seq[k]);
      i = 2; k++;
    } else if(--l == i) { // we have 2 leftover bp
      puts("2 left");
      seq[k] = codons[trntbl[(int)seqs[r][i]]]
                     [trntbl[(int)seqs[r][i+1]]]
                     [trntbl[(int)seqs[r+1][0]]];
      //      printf("codon: %c%c%c, p: %c\n", seqs[r][i], seqs[r][i+1], seqs[r+1][0], seq[k]);
      i = 1; k++;
    } else {
      i = 0;
    }
  }

  if(i < l) { seq[k] = 'X'; k++; } // we have leftover bp
  seq[k] = 0;

  if(strand == -1) { // If it's a rev strand, flip it in place
    tark_rev_seq(seq);
  }

  return seq;
}

int tark_iterator_translated_length(seq_iterator_t* siterator, int* remaining, int* unpadded_remaining) {
  int length;
  int remainder;

  length = siterator->seq_length / 3;
  if(siterator->seq_length % 3 != 0) length++;

  remainder = siterator->seq_length - siterator->seq_iterated;

  /* Receiving the amount remaining is optional,
     if the client gives NULL for remaining, we won't
     bother to calculate it. */
  if(remaining != NULL) {
    *remaining = remainder / 3;
    if(remainder % 3 != 0) (*remaining)++;
  }

  if(unpadded_remaining != NULL) {
    *unpadded_remaining = remainder / 3;
  }

  return length;
}

int tark_iterator_remaining(seq_iterator_t* siterator, int translated) {
  int remainder;

  remainder = siterator->seq_length - siterator->seq_iterated;

  if(translated) {
    return (remainder % 3 != 0) ? (remainder / 3) + 1 : (remainder / 3);
  }

  return remainder;
}

/*
   Create a interator for retrieving a sequence based on one or more
   locations in the reference

   Args
   [1] fai, pointer to faidx_t structure references
   [2] seq_name, const char* string with the sequence name
   [3] locations, const char* string with the location(s), format 1-100[,200-300,500-1000]
       commas are allowed in the location string. If NULL, we want the entire sequence,
       create an iterator of length 1 - [length of sequence]
   [4] ensembl_coords, integer, if true coordinates will be treated at Ensembl 1-base (1-100).
       if false they will be treated as GA4GH style 0-based (0-99)

   Return
   seq_iterator_t* or NULL, pointer to a seq_iterator object or NULL if failure
*/

seq_iterator_t* tark_fetch_iterator(faidx_t* fai, const char *seq_name, const char *locs, int ensembl_coords) {
  int c, i, l, k, location_end, beg, end, nseqs;
  seq_iterator_t* siterator;
  char* s;

  // If we don't actually have this sequence, return an error (NULL)
  if(!faidx_has_seq(fai, seq_name)) {
    return NULL;
  }

  /* Special case, if we're not given a set of locations, we assume we
     want the entire sequence. So create an iterator that covers that. */
  if(locs == NULL) {
    siterator = calloc(1, sizeof(seq_iterator_t));
    siterator->locations = malloc( sizeof(seq_location_t) );
    siterator->fai = fai;
    siterator->seq_name = strdup(seq_name);
    siterator->strand = 1;
    siterator->seq_length = faidx_seq_len(fai, seq_name);
    ((seq_location_t *)siterator->locations)->start = 0;
    ((seq_location_t *)siterator->locations)->end = siterator->seq_length - 1;
    ((seq_location_t *)siterator->locations)->length = siterator->seq_length;

    return siterator;
  }

  l = strlen(locs);
  s = (char*)malloc(l+1);
  // remove spaces and count the sequences, 
  // we could be fancier, but this is pretty much as efficient
  location_end = -1;
  nseqs = 1;
  for (i = k = 0; i < l; ++i) {
    if ( isspace(locs[i]) ) { continue; }
    if (locs[i] == ':') { location_end = i; break; }
    if (locs[i] == ',') { nseqs++; }
    s[k++] = locs[i];
  }
  s[k] = 0; l = k;
  //  puts("spaces removed\n");

  // Let's assume things are going to go well, make our iterator structure
  siterator = calloc(1, sizeof(seq_iterator_t));
  siterator->locations = malloc( sizeof(seq_location_t) * nseqs );

  // Let's start filling in the details
  siterator->fai = fai;
  siterator->seq_name = strdup(seq_name);
  if(location_end >= 0) {
    siterator->strand = atoi(locs + location_end + 1); // deal with strand later, if it's valid or not
  } // no strand is positive strand, tough.

  c = nseqs - 1;
  for(k--; k>=0; k--) {
    if(s[k] == '-') {
      end = atoi(s + k + 1);
      if (end > 0 && ensembl_coords) --end;
      s[k] = 0;
    } else if(s[k] == ',' || k == 0) {
      if( k == 0 ) k--; // Correct k for the last iteration so the ptr addition works
      beg = atoi(s + k + 1);
      if (beg > 0 && ensembl_coords) --beg;
      s[k] = 0;

      // Start must be less than end and
      // the end is less then the sequence length
      if( end < beg || 
	  faidx_seq_len(fai, seq_name) < end ) {
	tark_free_iterator(siterator);
	free(s);
	return NULL;
      }

      siterator->locations[c].start = beg;
      siterator->locations[c].end = end;
      siterator->locations[c].length = end - beg + 1;
      
      siterator->seq_length += siterator->locations[c].length;
      c--;
    }
  }

  free(s);
  return siterator;
}

void tark_free_iterator(seq_iterator_t* siterator) {
  if( siterator != NULL ) {
    if( siterator->locations != NULL ) {
      free(siterator->locations);
    }
    if( siterator->seq_name != NULL ) {
      free(siterator->seq_name);
    }
    free(siterator);
  }
}

char** tark_fetch_seqs(faidx_t* fai, const char *str, int *seq_len, int *nseqs, int *strand) {
  char *s;
  char **seqs;
  int c, i, l, k, name_end, len, beg, end;
  *strand = 1;
  *seq_len = 0;

  name_end = l = strlen(str);
  s = (char*)malloc(l+1);
  // remove spaces
  for (i = k = 0; i < l; ++i)
    if (! isspace(str[i]) ) s[k++] = str[i];
  s[k] = 0; l = k;
  //  puts("spaces removed\n");

  for(i = 0; i < l; i++) if(s[i] == ':') break; // Find the first colon
  if( i < l ) {
    name_end = i++;
    s[name_end] = 0;
    //    printf("name: %s\n", s);

    if(faidx_has_seq(fai, s)) {
      *nseqs = 1;
      for(k=i; k<l; k++) { // count the number of sequences we're fetching
	if(s[k] == ',') { (*nseqs)++; continue; }
	if(s[k] == ':') break;
      }
      //      printf("have %d seqs\n", *nseqs);
      //      printf("k: %d, l: %d, name_end: %d\n", k, l, name_end);

      // Make the array of sequences
      seqs = calloc(*nseqs, sizeof(char*));

      if(k < l) { // We found a colon, a strand
	s[k] = 0; // Null the colon
	//	printf("pre strand: %s\n", s + k + 1);

	*strand = atoi(s + k + 1); // deal with strand later, if it's valid or not
	//	printf("strand: %d\n", *strand);
      }

      c = *nseqs - 1;
      for(; k>=name_end; k--) {
	if(s[k] == '-') {
	  end = atoi(s + k + 1);
	  if (end > 0) --end;
	  s[k] = 0;
	} else if(s[k] == ',' || k == name_end) {
	  beg = atoi(s + k + 1);
	  if (beg > 0) --beg;
	  s[k] = 0;

	  seqs[c--] = faidx_fetch_seq(fai, s, beg, end, &len);
	  if(len > 0) *seq_len += len;
	}
      }

    } else {
      seqs = NULL;
    }
  } else {
    seqs = NULL;
  }

  free(s);

  return seqs;

}

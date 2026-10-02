#ifndef PFTC_VALID_H
#define PFTC_VALID_H

/* Full parse: 4 dot-separated octets, each 0-255. */
int is_valid_ipv4();

/* Decimal 0-32, no leading garbage/trailing chars. */
int is_valid_prefix();

/* Parses an already-validated "a.b.c.d" string into out[0..3]. Caller
   must have passed it through is_valid_ipv4() first - no range/format
   checking here. */
void parse_ipv4();

/* Parses an already-validated 0-32 prefix length string
   (is_valid_prefix()) into a byte. */
int parse_prefix();

#endif

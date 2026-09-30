#ifndef PFTC_VALID_H
#define PFTC_VALID_H

/* Full parse: 4 dot-separated octets, each 0-255. */
int is_valid_ipv4();

/* Decimal 0-32, no leading garbage/trailing chars. */
int is_valid_prefix();

#endif

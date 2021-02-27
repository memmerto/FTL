/* Pi-hole: A black hole for Internet advertisements
*  (c) 2020 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  EDNS0 parsing prototypes
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */
#ifndef EDNS0_HEADER
#define EDNS0_HEADER

#include "edns0.h"

struct edns_data {
	bool client_set;
	char client[ADDRSTRLEN];
	bool mac_set;
	char mac_byte[6];
	char mac_text[18];
};

void FTL_parse_pseudoheaders(struct dns_header *header, size_t n, union mysockaddr *peer, struct edns_data *edns);

#endif // EDNS0_HEADER

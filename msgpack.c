/* Pi-hole: A black hole for Internet advertisements
*  (c) 2017 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  MessagePack serialization
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "FTL.h"
#include "api.h"

void pack_eom(int sock) {
	// This byte is explicitly never used in the MessagePack spec, so it is perfect to use as an EOM for this API.
	unsigned char eom = 0xc1;
	swrite(sock, &eom, sizeof(eom));
}

void pack_number(int sock, unsigned char format, void *value, size_t size) {
	swrite(sock, &format, sizeof(format));
	swrite(sock, value, size);
}

void pack_uint8(int sock, uint8_t value) {
	pack_number(sock, 0xcc, &value, sizeof(value));
}

void pack_int32(int sock, int32_t value) {
	uint32_t bigEValue = htonl((uint32_t) value);
	pack_number(sock, 0xd2, &bigEValue, sizeof(bigEValue));
}

void pack_float(int sock, float value) {
	// Need to use memcpy to do a direct copy without reinterpreting the bytes. It should get optimized away.
	uint32_t bigEValue;
    memcpy(&bigEValue, &value, sizeof(bigEValue));
    bigEValue = htonl(bigEValue);
	pack_number(sock, 0xca, &bigEValue, sizeof(bigEValue));
}

void pack_map16_start(int sock, uint16_t length) {
	unsigned char format = 0xde;
	swrite(sock, &format, sizeof(format));
	uint16_t bigELength = htons(length);
	swrite(sock, &bigELength, sizeof(bigELength));
}

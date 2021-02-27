/* Pi-hole: A black hole for Internet advertisements
*  (c) 2017 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  API commands and MessagePack helpers
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */
#ifndef API_H
#define API_H

// Statistic methods
void getStats(const int *sock);
void getOverTime(const int *sock);
void getTopDomains(const char *client_message, const int *sock);
void getTopClients(const char *client_message, const int *sock);
void getUpstreamDestinations(const char *client_message, const int *sock);
void getQueryTypes(const int *sock);
void getAllQueries(const char *client_message, const int *sock);
void getRecentBlocked(const char *client_message, const int *sock);
void getQueryTypesOverTime(const int *sock);
void getClientsOverTime(const int *sock);
void getClientNames(const int *sock);

// FTL methods
void getClientID(const int *sock);
void getVersion(const int *sock);
void getDBstats(const int *sock);
void getUnknownQueries(const int *sock);

// DNS resolver methods (dnsmasq_interface.c)
void getCacheInformation(const int *sock);

// MessagePack serialization helpers
void pack_eom(const int sock);
void pack_bool(const int sock, const bool value);
void pack_uint8(const int sock, const uint8_t value);
void pack_uint64(const int sock, const uint64_t value);
void pack_int32(const int sock, const int32_t value);
void pack_int64(const int sock, const int64_t value);
void pack_float(const int sock, const float value);
bool pack_fixstr(const int sock, const char *string);
bool pack_str32(const int sock, const char *string);
void pack_map16_start(const int sock, const uint16_t length);

// DHCP lease management
void delete_lease(const char *client_message, const int *sock);

#endif // API_H

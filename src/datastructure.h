/* Pi-hole: A black hole for Internet advertisements
*  (c) 2019 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  Datastructure prototypes
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */
#ifndef DATASTRUCTURE_H
#define DATASTRUCTURE_H

// Definition of sqlite3_stmt
#include "database/sqlite3.h"

// Blocking status constants used by the domain->clientstatus vector
// We explicitly force UNKNOWN_BLOCKED to zero on all platforms as this is the
// default value set initially with calloc
enum blocking_status { UNKNOWN_BLOCKED = 0u, GRAVITY_BLOCKED, BLACKLIST_BLOCKED, REGEX_BLOCKED, WHITELISTED, NOT_BLOCKED } __attribute__ ((packed));

typedef struct {
	unsigned char magic;
	unsigned char type;
	unsigned char status;
	unsigned char privacylevel;
	unsigned char reply;
	unsigned char dnssec;
	time_t timestamp;
	int domainID;
	int clientID;
	int upstreamID;
	int id; // the ID is a (signed) int in dnsmasq, so no need for a long int here
	int CNAME_domainID; // only valid if query has a CNAME blocking status
	unsigned long response; // saved in units of 1/10 milliseconds (1 = 0.1ms, 2 = 0.2ms, 2500 = 250.0ms, etc.)
	int64_t db;
	unsigned int timeidx;
	bool whitelisted;
	bool complete;
} queriesData;

typedef struct {
	unsigned char magic;
	bool new;
	int count;
	int failed;
	size_t ippos;
	size_t namepos;
} upstreamsData;

typedef struct {
	unsigned char magic;
	bool new;
	bool found_group;
	int count;
	int blockedcount;
	int overTime[OVERTIME_SLOTS];
	unsigned int numQueriesARP;
	size_t groupspos;
	size_t ippos;
	size_t namepos;
	time_t lastQuery;
} clientsData;

typedef struct {
	unsigned char magic;
	size_t domainpos;
	int count;
	int blockedcount;
} domainsData;

typedef struct {
	unsigned char magic;
	unsigned char force_reply;
	enum blocking_status blocking_status;
	enum query_type query_type;
	int domainID;
	int clientID;
	int black_regex_idx;
} DNSCacheData;

void strtolower(char *str);
int findUpstreamID(const char * upstream, const bool count);
int findDomainID(const char *domain, const bool count);
int findClientID(const char *client, const bool count);
int findCacheID(int domainID, int clientID, enum query_type query_type);
bool isValidIPv4(const char *addr);
bool isValidIPv6(const char *addr);

void FTL_reload_all_domainlists(void);
void FTL_reset_per_client_domain_data(void);

const char *getDomainString(const queriesData* query);
const char *getCNAMEDomainString(const queriesData* query);
const char *getClientIPString(const queriesData* query);
const char *getClientNameString(const queriesData* query);

// Pointer getter functions
#define getQuery(queryID, checkMagic) _getQuery(queryID, checkMagic, __LINE__, __FUNCTION__, __FILE__)
queriesData* _getQuery(int queryID, bool checkMagic, int line, const char * function, const char * file);
#define getClient(clientID, checkMagic) _getClient(clientID, checkMagic, __LINE__, __FUNCTION__, __FILE__)
clientsData* _getClient(int clientID, bool checkMagic, int line, const char * function, const char * file);
#define getDomain(domainID, checkMagic) _getDomain(domainID, checkMagic, __LINE__, __FUNCTION__, __FILE__)
domainsData* _getDomain(int domainID, bool checkMagic, int line, const char * function, const char * file);
#define getUpstream(upstreamID, checkMagic) _getUpstream(upstreamID, checkMagic, __LINE__, __FUNCTION__, __FILE__)
upstreamsData* _getUpstream(int upstreamID, bool checkMagic, int line, const char * function, const char * file);
#define getDNSCache(cacheID, checkMagic) _getDNSCache(cacheID, checkMagic, __LINE__, __FUNCTION__, __FILE__)
DNSCacheData* _getDNSCache(int cacheID, bool checkMagic, int line, const char * function, const char * file);

#endif //DATASTRUCTURE_H

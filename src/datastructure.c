/* Pi-hole: A black hole for Internet advertisements
*  (c) 2017 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  Query processing routines
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "FTL.h"
#include "datastructure.h"
#include "shmem.h"
#include "log.h"
// enum REGEX
#include "regex_r.h"
// reload_per_client_regex()
#include "database/gravity-db.h"
// flush_message_table()
#include "database/message-table.h"
// bool startup
#include "main.h"
// reset_aliasclient()
#include "database/aliasclients.h"
// piholeFTLDB_reopen()
#include "database/common.h"
// config struct
#include "config.h"
// set_event(RESOLVE_NEW_HOSTNAMES)
#include "events.h"

const char *querytypes[TYPE_MAX] = {"UNKNOWN", "A", "AAAA", "ANY", "SRV", "SOA", "PTR", "TXT",
                                    "NAPTR", "MX", "DS", "RRSIG", "DNSKEY", "NS", "OTHER", "SVCB",
                                    "HTTPS"};

// converts upper to lower case, and leaves other characters unchanged
void strtolower(char *str)
{
	int i = 0;
	while(str[i]){ str[i] = tolower(str[i]); i++; }
}

int findQueryID(const int id)
{
	// Loop over all queries - we loop in reverse order (start from the most recent query and
	// continuously walk older queries while trying to find a match. Ideally, we should always
	// find the correct query with zero iterations, but it may happen that queries are processed
	// asynchronously, e.g. for slow upstream relies to a huge amount of requests.
	// We iterate from the most recent query down to at most MAXITER queries in the past to avoid
	// iterating through the entire array of queries
	// MAX(0, a) is used to return 0 in case a is negative (negative array indices are harmful)
	const int until = MAX(0, counters->queries-MAXITER);
	const int start = MAX(0, counters->queries-1);

	// Check UUIDs of queries
	for(int i = start; i >= until; i--)
	{
		const queriesData* query = getQuery(i, true);

		// Check if the returned pointer is valid before trying to access it
		if(query == NULL)
			continue;

		if(query->id == id)
			return i;
	}

	// If not found
	return -1;
}

int findUpstreamID(const char * upstreamString, const in_port_t port)
{
	// Go through already knows upstream servers and see if we used one of those
	for(int upstreamID=0; upstreamID < counters->upstreams; upstreamID++)
	{
		// Get upstream pointer
		upstreamsData* upstream = getUpstream(upstreamID, true);

		// Check if the returned pointer is valid before trying to access it
		if(upstream == NULL)
			continue;

		if(strcmp(getstr(upstream->ippos), upstreamString) == 0 && upstream->port == port)
			return upstreamID;
	}
	// This upstream server is not known
	// Store ID
	const int upstreamID = counters->upstreams;
	logg("New upstream server: %s:%u (%i/%u)", upstreamString, port, upstreamID, counters->upstreams_MAX);

	// Check struct size
	memory_check(UPSTREAMS);

	// Get upstream pointer
	upstreamsData* upstream = getUpstream(upstreamID, false);
	if(upstream == NULL)
	{
		logg("ERROR: Encountered serious memory error in findupstreamID()");
		return -1;
	}

	// Set magic byte
	upstream->magic = MAGICBYTE;
	// Initialize its counter
	upstream->count = 0;
	// Save upstream destination IP address
	upstream->ippos = addstr(upstreamString);
	upstream->failed = 0;
	// Initialize upstream hostname
	// Due to the nature of us being the resolver,
	// the actual resolving of the host name has
	// to be done separately to be non-blocking
	upstream->new = true;
	upstream->namepos = 0; // 0 -> string with length zero
	set_event(RESOLVE_NEW_HOSTNAMES);
	// This is a new upstream server
	upstream->lastQuery = time(NULL);
	// Store port
	upstream->port = port;
	// Increase counter by one
	counters->upstreams++;

	return upstreamID;
}

int findDomainID(const char *domainString, const bool count)
{
	for(int domainID = 0; domainID < counters->domains; domainID++)
	{
		// Get domain pointer
		domainsData* domain = getDomain(domainID, true);

		// Check if the returned pointer is valid before trying to access it
		if(domain == NULL)
			continue;

		// Quick test: Does the domain start with the same character?
		if(getstr(domain->domainpos)[0] != domainString[0])
			continue;

		// If so, compare the full domain using strcmp
		if(strcmp(getstr(domain->domainpos), domainString) == 0)
		{
			if(count)
				domain->count++;
			return domainID;
		}
	}

	// If we did not return until here, then this domain is not known
	// Store ID
	const int domainID = counters->domains;

	// Check struct size
	memory_check(DOMAINS);

	// Get domain pointer
	domainsData* domain = getDomain(domainID, false);
	if(domain == NULL)
	{
		logg("ERROR: Encountered serious memory error in findDomainID()");
		return -1;
	}

	// Set magic byte
	domain->magic = MAGICBYTE;
	// Set its counter to 1 only if this domain is to be counted
	// Domains only encountered during CNAME inspection are NOT counted here
	domain->count = count ? 1 : 0;
	// Set blocked counter to zero
	domain->blockedcount = 0;
	// Store domain name - no need to check for NULL here as it doesn't harm
	domain->domainpos = addstr(domainString);
	// Increase counter by one
	counters->domains++;

	return domainID;
}

int findClientID(const char *clientIP, const bool count, const bool aliasclient)
{
	// Compare content of client against known client IP addresses
	for(int clientID=0; clientID < counters->clients; clientID++)
	{
		// Get client pointer
		clientsData* client = getClient(clientID, true);

		// Check if the returned pointer is valid before trying to access it
		if(client == NULL)
			continue;

		// Quick test: Does the clients IP start with the same character?
		if(getstr(client->ippos)[0] != clientIP[0])
			continue;

		// If so, compare the full IP using strcmp
		if(strcmp(getstr(client->ippos), clientIP) == 0)
		{
			// Add one if count == true (do not add one, e.g., during ARP table processing)
			if(count && !aliasclient) change_clientcount(client, 1, 0, -1, 0);
			return clientID;
		}
	}

	// Return -1 (= not found) if count is false because we do not want to create a new client here
	// Proceed if we are looking for a alias-client because we want to create a new record
	if(!count && !aliasclient)
		return -1;

	// If we did not return until here, then this client is definitely new
	// Store ID
	const int clientID = counters->clients;

	// Check struct size
	memory_check(CLIENTS);

	// Get client pointer
	clientsData* client = getClient(clientID, false);
	if(client == NULL)
	{
		logg("ERROR: Encountered serious memory error in findClientID()");
		return -1;
	}

	// Set magic byte
	client->magic = MAGICBYTE;
	// Set its counter to 1
	client->count = (count && !aliasclient)? 1 : 0;
	// Initialize blocked count to zero
	client->blockedcount = 0;
	// Store client IP - no need to check for NULL here as it doesn't harm
	client->ippos = addstr(clientIP);
	// Initialize client hostname
	// Due to the nature of us being the resolver,
	// the actual resolving of the host name has
	// to be done separately to be non-blocking
	client->flags.new = true;
	client->namepos = 0;
	set_event(RESOLVE_NEW_HOSTNAMES);
	// No query seen so far
	client->lastQuery = 0;
	client->numQueriesARP = client->count;
	// Configured groups are yet unknown
	client->flags.found_group = false;
	client->groupspos = 0u;
	// Store time this client was added, we re-read group settings
	// some time after adding a client to ensure we pick up possible
	// group configuration though hostname, MAC address or interface
	client->reread_groups = 0u;
	client->firstSeen = time(NULL);
	// Interface is not yet known
	client->ifacepos = 0;
	// Set all MAC address bytes to zero
	client->hwlen = -1;
	memset(client->hwaddr, 0, sizeof(client->hwaddr));
	// This may be a alias-client, the ID is set elsewhere
	client->flags.aliasclient = aliasclient;
	client->aliasclient_id = -1;

	// Initialize client-specific overTime data
	memset(client->overTime, 0, sizeof(client->overTime));

	// Store client ID
	client->id = clientID;

	// Increase counter by one
	counters->clients++;

	// Get groups for this client and set enabled regex filters
	// Note 1: We do this only after increasing the clients counter to
	//         ensure sufficient shared memory is available in the
	//         pre_client_regex object.
	// Note 2: We don't do this before starting up is done as the gravity
	//         database may not be available. All clients initialized
	//         during history reading get their enabled regexs reloaded
	//         in the initial call to FTL_reload_all_domainlists()
	if(!startup && !aliasclient)
		reload_per_client_regex(client);

	// Check if this client is managed by a alias-client
	if(!aliasclient)
		reset_aliasclient(client);

	return clientID;
}

void change_clientcount(clientsData *client, int total, int blocked, int overTimeIdx, int overTimeMod)
{
		client->count += total;
		client->blockedcount += blocked;
		if(overTimeIdx > -1 && overTimeIdx < OVERTIME_SLOTS)
			client->overTime[overTimeIdx] += overTimeMod;

		// Also add counts to the conencted alias-client (if any)
		if(client->flags.aliasclient)
		{
			logg("WARN: Should not add to alias-client directly (client \"%s\" (%s))!",
			     getstr(client->namepos), getstr(client->ippos));
			return;
		}
		if(client->aliasclient_id > -1)
		{
			clientsData *aliasclient = getClient(client->aliasclient_id, true);
			aliasclient->count += total;
			aliasclient->blockedcount += blocked;
			if(overTimeIdx > -1 && overTimeIdx < OVERTIME_SLOTS)
				aliasclient->overTime[overTimeIdx] += overTimeMod;
		}
}

int findCacheID(int domainID, int clientID, enum query_types query_type)
{
	// Compare content of client against known client IP addresses
	for(int cacheID = 0; cacheID < counters->dns_cache_size; cacheID++)
	{
		// Get cache pointer
		DNSCacheData* dns_cache = getDNSCache(cacheID, true);

		// Check if the returned pointer is valid before trying to access it
		if(dns_cache == NULL)
			continue;

		if(dns_cache->domainID == domainID &&
		   dns_cache->clientID == clientID &&
		   dns_cache->query_type == query_type)
		{
			return cacheID;
		}
	}

	// Get ID of new cache entry
	const int cacheID = counters->dns_cache_size;

	// Check struct size
	memory_check(DNS_CACHE);

	// Get client pointer
	DNSCacheData* dns_cache = getDNSCache(cacheID, false);

	if(dns_cache == NULL)
	{
		logg("ERROR: Encountered serious memory error in findCacheID()");
		return -1;
	}

	// Initialize cache entry
	dns_cache->magic = MAGICBYTE;
	dns_cache->blocking_status = UNKNOWN_BLOCKED;
	dns_cache->domainID = domainID;
	dns_cache->clientID = clientID;
	dns_cache->query_type = query_type;
	dns_cache->force_reply = 0u;

	// Increase counter by one
	counters->dns_cache_size++;

	return cacheID;
}

bool isValidIPv4(const char *addr)
{
	struct sockaddr_in sa;
	return inet_pton(AF_INET, addr, &(sa.sin_addr)) != 0;
}

bool isValidIPv6(const char *addr)
{
	struct sockaddr_in6 sa;
	return inet_pton(AF_INET6, addr, &(sa.sin6_addr)) != 0;
}

// Privacy-level sensitive subroutine that returns the domain name
// only when appropriate for the requested query
const char *getDomainString(const queriesData* query)
{
	// Check if the returned pointer is valid before trying to access it
	if(query == NULL)
		return "";

	if(query->privacylevel < PRIVACY_HIDE_DOMAINS)
	{
		// Get domain pointer
		const domainsData* domain = getDomain(query->domainID, true);

		// Return string
		return getstr(domain->domainpos);
	}
	else
		return HIDDEN_DOMAIN;
}

// Privacy-level sensitive subroutine that returns the domain name
// only when appropriate for the requested query
const char *getCNAMEDomainString(const queriesData* query)
{
	// Check if the returned pointer is valid before trying to access it
	if(query == NULL)
		return "";

	if(query->privacylevel < PRIVACY_HIDE_DOMAINS)
	{
		// Get domain pointer
		const domainsData* domain = getDomain(query->CNAME_domainID, true);

		// Return string
		return getstr(domain->domainpos);
	}
	else
		return HIDDEN_DOMAIN;
}

// Privacy-level sensitive subroutine that returns the client IP
// only when appropriate for the requested query
const char *getClientIPString(const queriesData* query)
{
	// Check if the returned pointer is valid before trying to access it
	if(query == NULL)
		return "";

	if(query->privacylevel < PRIVACY_HIDE_DOMAINS_CLIENTS)
	{
		// Get client pointer
		const clientsData* client = getClient(query->clientID, false);

		// Return string
		return getstr(client->ippos);
	}
	else
		return HIDDEN_CLIENT;
}

// Privacy-level sensitive subroutine that returns the client host name
// only when appropriate for the requested query
const char *getClientNameString(const queriesData* query)
{
	// Check if the returned pointer is valid before trying to access it
	if(query == NULL)
		return "";

	if(query->privacylevel < PRIVACY_HIDE_DOMAINS_CLIENTS)
	{
		// Get client pointer
		const clientsData* client = getClient(query->clientID, true);

		// Return string
		return getstr(client->namepos);
	}
	else
		return HIDDEN_CLIENT;
}

void FTL_reset_per_client_domain_data(void)
{
	if(config.debug & DEBUG_DATABASE)
		logg("Resetting per-client DNS cache, size is %i", counters->dns_cache_size);

	for(int cacheID = 0; cacheID < counters->dns_cache_size; cacheID++)
	{
		// Reset all blocking yes/no fields for all domains and clients
		// This forces a reprocessing of all available filters for any
		// given domain and client the next time they are seen
		DNSCacheData *dns_cache = getDNSCache(cacheID, true);
		if(dns_cache != NULL)
			dns_cache->blocking_status = UNKNOWN_BLOCKED;
	}
}

void FTL_reload_all_domainlists(void)
{
	lock_shm();

	// (Re-)open FTL database connection
	piholeFTLDB_reopen();

	// Flush messages stored in the long-term database
	flush_message_table();

	// (Re-)open gravity database connection
	gravityDB_reopen();

	// Reset number of blocked domains
	counters->gravity = gravityDB_count(GRAVITY_TABLE);

	// Read and compile possible regex filters
	// only after having called gravityDB_open()
	read_regex_from_database();

	// Reset FTL's internal DNS cache storing whether a specific domain
	// has already been validated for a specific user
	FTL_reset_per_client_domain_data();

	unlock_shm();
}

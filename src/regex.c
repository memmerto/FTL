/* Pi-hole: A black hole for Internet advertisements
*  (c) 2017 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  Regular Expressions
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "FTL.h"
#include "regex_r.h"
#include "timers.h"
#include "log.h"
#include "config.h"
// data getter functions
#include "datastructure.h"
#include "database/gravity-db.h"
// add_per_client_regex_client()
#include "shmem.h"
#include "database/message-table.h"
// init_shmem()
#include "shmem.h"
// read_FTL_config()
#include "config.h"
// cli_stuff()
#include "args.h"

const char *regextype[REGEX_MAX] = { "blacklist", "whitelist", "CLI" };

static regexData *white_regex = NULL;
static regexData *black_regex = NULL;
static regexData   *cli_regex = NULL;
static unsigned int num_regex[REGEX_MAX] = { 0 };
unsigned int regex_change = 0;

static inline regexData *get_regex_ptr(const enum regex_type regexid)
{
	switch (regexid)
	{
		case REGEX_BLACKLIST:
			return black_regex;
		case REGEX_WHITELIST:
			return white_regex;
		case REGEX_CLI:
			return cli_regex;
		case REGEX_MAX: // Fall through
		default: // This is not possible
			return NULL;
	}
}

static inline void free_regex_ptr(const enum regex_type regexid)
{
	regexData **regex;
	switch (regexid)
	{
		case REGEX_BLACKLIST:
			regex = &black_regex;
			break;
		case REGEX_WHITELIST:
			regex = &white_regex;
			break;
		case REGEX_CLI:
			regex = &cli_regex;
			break;
		case REGEX_MAX: // Fall through
		default: // This is not possible
			return;
	}

	// Free pointer (if not already NULL)
	if(*regex != NULL)
	{
		free(*regex);
		*regex = NULL;
	}
}

unsigned int __attribute__((pure)) get_num_regex(const enum regex_type regexid)
{
	// count number of all available reges
	if(regexid == REGEX_MAX)
	{
		unsigned int num = 0;
		for(unsigned int i = 0; i < REGEX_MAX; i++)
			num += num_regex[i];
		return num;
	}

	// else: specific regex type
	return num_regex[regexid];
}

#define FTL_REGEX_SEP ";"
/* Compile regular expressions into data structures that can be used with
   regexec() to match against a string */
static bool compile_regex(const char *regexin, const enum regex_type regexid)
{
	regexData *regex = get_regex_ptr(regexid);
	int index = num_regex[regexid]++;

	// Extract possible Pi-hole extensions
	char rgxbuf[strlen(regexin) + 1u];
	// Parse special FTL syntax if present
	if(strstr(regexin, FTL_REGEX_SEP) != NULL)
	{
		char *buf = strdup(regexin);
		// Extract regular expression pattern in front of FTL-specific syntax
		char *saveptr = NULL;
		char *part = strtok_r(buf, FTL_REGEX_SEP, &saveptr);
		strncpy(rgxbuf, part, sizeof(rgxbuf));

		// Analyze FTL-specific parts
		while((part = strtok_r(NULL, FTL_REGEX_SEP, &saveptr)) != NULL)
		{
			char extra[17] = { 0 };
			// options ";querytype=!AAAA" and ";querytype=AAAA"
			if(sscanf(part, "querytype=%16s", extra))
			{
				// Warn if specified more than one querytype option
				if(regex[index].query_type != 0)
					logg_regex_warning(regextype[regexid],
					                   "Overwriting previous querytype setting",
					                   regex[index].database_id, regexin);

				// Test input string against all implemented query types
				for(enum query_types type = TYPE_A; type < TYPE_MAX; type++)
				{
					// Check for querytype
					if(strcasecmp(extra, querytypes[type]) == 0)
					{
						regex[index].query_type = type;
						regex[index].query_type_inverted = false;
						break;
					}
					// Check for INVERTED querytype
					else if(extra[0] == '!' && strcasecmp(extra + 1u, querytypes[type]) == 0)
					{
						regex[index].query_type = type;
						regex[index].query_type_inverted = true;
						break;
					}
				}
				// Nothing found
				if(regex[index].query_type == 0)
					logg_regex_warning(regextype[regexid], "Unknown querytype",
					                   regex[index].database_id, regexin);

				// Debug output
				else if(config.debug & DEBUG_REGEX)
				{
					logg("   This regex will %s match query type %s",
					     regex[index].query_type_inverted ? "NOT" : "ONLY",
					     querytypes[regex[index].query_type]);
				}
			}
			// option: ";invert"
			else if(strcasecmp(part, "invert") == 0)
			{
				regex[index].inverted = true;

				// Debug output
				if(config.debug & DEBUG_REGEX)
				{
					logg("   This regex will match in inverted mode.");
				}
			}
			else
			{
				logg("   Option \"%s\" not known, ignoring it.", part);
			}
		}
		free(buf);
	}
	else
	{
		// Copy entire input string into buffer
		strcpy(rgxbuf, regexin);
	}

	// We use the extended RegEx flavor (ERE) and specify that matching should
	// always be case INsensitive
	const int errcode = regcomp(&regex[index].regex, rgxbuf, REG_EXTENDED | REG_ICASE | REG_NOSUB);
	if(errcode != 0)
	{
		// Get error string and log it
		const size_t length = regerror(errcode, &regex[index].regex, NULL, 0);
		char *buffer = calloc(length, sizeof(char));
		(void) regerror (errcode, &regex[index].regex, buffer, length);
		logg_regex_warning(regextype[regexid], buffer, regex[index].database_id, regexin);
		free(buffer);
		regex[index].available = false;
		return false;
	}

	// Store compiled regex string in buffer
	regex[index].string = strdup(regexin);
	regex[index].available = true;

	return true;
}

int match_regex(const char *input, const DNSCacheData* dns_cache, const int clientID,
                const enum regex_type regexid, const bool regextest)
{
	int match_idx = -1;
	regexData *regex = get_regex_ptr(regexid);
#ifdef USE_TRE_REGEX
	regmatch_t match = { 0 }; // This also disables any sub-matching
#endif

	// Check if we need to recompile regex because they were changed in
	// another fork. If this is the case, reload everything (regex
	// themselves as well as per-client enabled/disabled state)
	if(regex_change != counters->regex_change)
	{
		logg("Reloading externally changed regular expressions");
		read_regex_from_database();
		// Update regex pointer as it will have changed (free_regex has
		// been called)
		regex = get_regex_ptr(regexid);
	}

	// Loop over all configured regex filters of this type
	for(unsigned int index = 0; index < num_regex[regexid]; index++)
	{
		// Only check regex which have been successfully compiled ...
		if(!regex[index].available)
		{
			if(config.debug & DEBUG_REGEX)
			{
				logg("Regex %s (%u, DB ID %d) \"%s\" is NOT AVAILABLE",
				     regextype[regexid], index, regex[index].database_id,
				     regex[index].string);
			}
			continue;
		}
		// ... and are enabled for this client
		int regexID = index;
		if(regexid == REGEX_WHITELIST)
			regexID += num_regex[REGEX_BLACKLIST];
		else if(regexid == REGEX_CLI)
			regexID += num_regex[REGEX_BLACKLIST] +
			           num_regex[REGEX_WHITELIST];

		// Only use regular expressions enabled for this client
		// We allow clientID = -1 to get all regex (for testing)
		if(clientID >= 0 && !get_per_client_regex(clientID, regexID))
		{
			if(config.debug & DEBUG_REGEX)
			{
				clientsData* client = getClient(clientID, true);
				if(client != NULL)
				{
					logg("Regex %s (%u, DB ID %d) \"%s\" NOT ENABLED for client %s",
					     regextype[regexid], index, regex[index].database_id,
					     regex[index].string, getstr(client->ippos));
				}
			}
			continue;
		}

		// Try to match the compiled regular expression against input
		if(config.debug & DEBUG_REGEX)
			logg("Executing: index = %d, preg = %p, str = \"%s\", pmatch = %p", index, &regex[index].regex, input, &match);
#ifdef USE_TRE_REGEX
		int retval = tre_regexec(&regex[index].regex, input, 0, &match, 0);
#else
		int retval = regexec(&regex[index].regex, input, 0, NULL, 0);
#endif
		// regexec() returns REG_OK for a successful match or REG_NOMATCH for failure.
		if ((retval == REG_OK && !regex[index].inverted) ||
		    (retval == REG_NOMATCH && regex[index].inverted))
		{
			// Check possible additional regex settings
			if(dns_cache != NULL)
			{
				// Check query type filtering
				if(regex[index].query_type != 0)
				{
					if((!regex[index].query_type_inverted && regex[index].query_type != dns_cache->query_type) ||
					    (regex[index].query_type_inverted && regex[index].query_type == dns_cache->query_type))
					{
						if(config.debug & DEBUG_REGEX)
						{
							logg("Regex %s (%u, DB ID %i) NO match: \"%s\" vs. \"%s\""
								" (skipped because of query type %smatch)",
							regextype[regexid], index, regex[index].database_id,
							input, regex[index].string, regex[index].query_type_inverted ? "inversion " : "mis");
						}
						continue;
					}
				}
			}

			// Match, return true
			match_idx = regex[index].database_id;

			// Print match message when in regex debug mode
			if(config.debug & DEBUG_REGEX)
			{
				// Approximate regex matching mode
				logg("Regex %s (%u, DB ID %i) >> MATCH: \"%s\" vs. \"%s\"",
				     regextype[regexid], index, regex[index].database_id,
				     input, regex[index].string);
			}

			if(regextest && regexid == REGEX_CLI)
			{
				// CLI provided regular expression
				logg("    %s%s%s matches",
				     cli_bold(), regex[index].string, cli_normal());
			}
			else if(regextest && regexid == REGEX_BLACKLIST)
			{
				// Database-sourced regular expression
				logg("    %s%s%s matches (regex blacklist, DB ID %i)",
				     cli_bold(), regex[index].string, cli_normal(),
				     regex[index].database_id);
			}
			else if(regextest && regexid == REGEX_WHITELIST)
			{
				// Database-sourced regular expression
				logg("    %s%s%s matches (regex whitelist, DB ID %i)",
				     cli_bold(), regex[index].string, cli_normal(),
				     regex[index].database_id);
			}
			else
			{
				// Only check the first match when not in regex-test mode
				break;
			}
		}

		// Print no match message when in regex debug mode
		if(config.debug & DEBUG_REGEX && match_idx == -1)
		{
			logg("Regex %s (%u, DB ID %i) NO match: \"%s\" vs. \"%s\"",
			     regextype[regexid], index, regex[index].database_id,
			     input, regex[index].string);
		}
	}

	// No match, no error, return false
	return match_idx;
}

static void free_regex(void)
{
	// Return early if we don't use any regex filters
	if(white_regex == NULL &&
	   black_regex == NULL &&
	     cli_regex == NULL)
	{
		if(config.debug & DEBUG_DATABASE)
			logg("Not using any regex filters, nothing to free or reset");
		return;
	}

	// Reset client configuration
	if(config.debug & DEBUG_DATABASE)
		logg("Resetting per-client regex settings");
	for(int clientID = 0; clientID < counters->clients; clientID++)
	{
		reset_per_client_regex(clientID);
	}

	// Free regex datastructure
	// Loop over regex types
	for(enum regex_type regexid = REGEX_BLACKLIST; regexid < REGEX_MAX; regexid++)
	{
		regexData *regex = get_regex_ptr(regexid);

		// Reset counter for number of regex
		const unsigned int oldcount = num_regex[regexid];
		num_regex[regexid] = 0;

		// Exit early if the regex has already been freed (or has never been used)
		if(regex == NULL)
			continue;

		if(config.debug & DEBUG_DATABASE)
		{
			logg("Going to free %i entries in %s regex struct",
			     oldcount, regextype[regexid]);
		}

		// Loop over entries with this regex type
		for(unsigned int index = 0; index < oldcount; index++)
		{
			if(!regex[index].available)
				continue;

			regfree(&regex[index].regex);

			// Also free buffered regex strings
			if(regex[index].string != NULL)
			{
				free(regex[index].string);
				regex[index].string = NULL;
			}
		}

		if(config.debug & DEBUG_DATABASE)
		{
			logg("Loop done, freeing regex pointer (%p)", regex);
		}

		// Free array with regex datastructure
		free_regex_ptr(regexid);
	}
}

// This function does three things:
//   1. Allocate additional memory if required
//   2. Reset all regex to false for this client
//   3. Load regex enabled/disabled state
void reload_per_client_regex(clientsData *client)
{
	// Ensure there is enough memory in the shared memory object
	add_per_client_regex(client->id);

	// Zero-initialize (or wipe previous) regex
	reset_per_client_regex(client->id);

	// Load regex per-group regex blacklist for this client
	if(num_regex[REGEX_BLACKLIST] > 0)
		gravityDB_get_regex_client_groups(client, num_regex[REGEX_BLACKLIST],
		                                  black_regex, REGEX_BLACKLIST,
		                                  "vw_regex_blacklist");

	// Load regex per-group regex whitelist for this client
	if(num_regex[REGEX_WHITELIST] > 0)
		gravityDB_get_regex_client_groups(client, num_regex[REGEX_WHITELIST],
		                                  white_regex, REGEX_WHITELIST,
		                                  "vw_regex_whitelist");
}

static void read_regex_table(const enum regex_type regexid)
{
	// Get table ID
	const enum gravity_tables tableID = (regexid == REGEX_BLACKLIST) ? REGEX_BLACKLIST_TABLE : REGEX_WHITELIST_TABLE;

	if(config.debug & DEBUG_DATABASE)
		logg("Reading regex %s from database", regextype[regexid]);

	// Get number of lines in the regex table
	num_regex[regexid] = 0;
	int count = gravityDB_count(tableID);

	if(count == 0)
	{
		return;
	}
	else if(count < 0)
	{
		logg("WARN: Database query failed, assuming there are no %s regex entries", regextype[regexid]);
		return;
	}

	// Allocate memory for regex
	regexData *regex = NULL;
	if(regexid == REGEX_BLACKLIST)
	{
		black_regex = calloc(count, sizeof(regexData));
		regex = black_regex;
	}
	else
	{
		white_regex = calloc(count, sizeof(regexData));
		regex = white_regex;
	}

	// Connect to regex table
	if(!gravityDB_getTable(tableID))
	{
		logg("read_regex_from_database(): Error getting %s regex table from database",
		     regextype[regexid]);
		return;
	}

	// Walk database table
	const char *domain = NULL;
	int rowid = 0;
	while((domain = gravityDB_getDomain(&rowid)) != NULL)
	{
		// Avoid buffer overflow if database table changed
		// since we counted its entries
		if(num_regex[regexid] >= (unsigned int)count)
		{
			logg("INFO: read_regex_table(%s) exiting early to avoid overflow (%d/%d).",
			     regextype[regexid], num_regex[regexid], count);
			break;
		}

		// Skip this entry if empty: an empty regex filter would match
		// anything anywhere and hence match all incoming domains. A user
		// can still achieve this with a filter such as ".*", however empty
		// filters in the regex table are probably not expected to have such
		// an effect and would immediately lead to "blocking or whitelisting
		// the entire Internet"
		if(strlen(domain) < 1)
			continue;

		// Compile this regex
		if(config.debug & DEBUG_REGEX)
		{
			logg("Compiling %s regex %i (DB ID %i): %s",
			     regextype[regexid], num_regex[regexid], rowid, domain);
		}

		compile_regex(domain, regexid);
		regex[num_regex[regexid]-1].database_id = rowid;

		// Signal other forks that the regex data has changed and should be updated
		regex_change = ++counters->regex_change;
	}

	// Finalize statement and close gravity database handle
	gravityDB_finalizeTable();

	if(config.debug & DEBUG_DATABASE)
	{
		logg("Read %i %s regex entries",
		     num_regex[regexid],
		     regextype[regexid]);
	}
}

void read_regex_from_database(void)
{
	// Free regex filters
	// This routine is safe to be called even when there
	// are no regex filters at the moment
	free_regex();

	// Start timer for regex compilation analysis
	timer_start(REGEX_TIMER);

	// Read and compile regex blacklist
	read_regex_table(REGEX_BLACKLIST);

	// Read and compile regex whitelist
	read_regex_table(REGEX_WHITELIST);

	// Loop over all clients and ensure we have enough space and load
	// per-client regex data, not all of the regex read and compiled above
	// will also be used by all clients
	if(config.debug & DEBUG_DATABASE)
		logg("Loading per-client regex data");
	for(int clientID = 0; clientID < counters->clients; clientID++)
	{
		// Get client pointer
		clientsData *client = getClient(clientID, true);
		// Skip invalid and alias-clients
		if(client == NULL || client->flags.aliasclient)
			continue;

		reload_per_client_regex(client);
	}

	// Print message to FTL's log after reloading regex filters
	logg("Compiled %i whitelist and %i blacklist regex filters for %i clients in %.1f msec",
	     num_regex[REGEX_WHITELIST], num_regex[REGEX_BLACKLIST],
	     counters->clients, timer_elapsed_msec(REGEX_TIMER));
}

int regex_test(const bool debug_mode, const bool quiet, const char *domainin, const char *regexin)
{
	// Prepare counters and regex memories
	counters = calloc(1, sizeof(countersStruct));
	// Disable terminal output during config config file parsing
	log_ctrl(false, false);
	// Process pihole-FTL.conf to get gravity.db
	read_FTLconf();

	// Disable all debugging output if not explicitly in debug mode (CLI argument "d")
	if(!debug_mode)
		config.debug = 0;
	// Re-enable terminal output
	log_ctrl(false, !quiet);

	int matchidx = -1;
	if(regexin == NULL)
	{
		// Read and compile regex lists from database
		logg("%s Loading regex filters from database...", cli_info());
		timer_start(REGEX_TIMER);
		log_ctrl(false, true); // Temporarily re-enable terminal output for error logging
		read_regex_table(REGEX_BLACKLIST);
		read_regex_table(REGEX_WHITELIST);
		log_ctrl(false, !quiet); // Re-apply quiet option after compilation
		logg("    Compiled %i black- and %i whitelist regex filters in %.3f msec\n",
		     num_regex[REGEX_BLACKLIST],
		     num_regex[REGEX_WHITELIST],
		     timer_elapsed_msec(REGEX_TIMER));

		// Check user-provided domain against all loaded regular blacklist expressions
		logg("%s Checking domain against blacklist...", cli_info());
		timer_start(REGEX_TIMER);
		int matchidx1 = match_regex(domainin, NULL, -1, REGEX_BLACKLIST, true);
		logg("    Time: %.3f msec", timer_elapsed_msec(REGEX_TIMER));

		// Check user-provided domain against all loaded regular whitelist expressions
		logg("%s Checking domain against whitelist...", cli_info());
		timer_start(REGEX_TIMER);
		int matchidx2 = match_regex(domainin, NULL, -1, REGEX_WHITELIST, true);
		logg("    Time: %.3f msec", timer_elapsed_msec(REGEX_TIMER));
		matchidx = MAX(matchidx1, matchidx2);

	}
	else
	{
		// Compile CLI regex
		logg("%s Compiling regex filter...", cli_info());
		cli_regex = calloc(1, sizeof(regexData));

		// Compile CLI regex
		timer_start(REGEX_TIMER);
		log_ctrl(false, true); // Temporarily re-enable terminal output for error logging
		if(!compile_regex(regexin, REGEX_CLI))
			return EXIT_FAILURE;
		log_ctrl(false, !quiet); // Re-apply quiet option after compilation
		logg("    Compiled regex filter in %.3f msec\n", timer_elapsed_msec(REGEX_TIMER));

		// Check user-provided domain against user-provided regular expression
		logg("Checking domain...");
		timer_start(REGEX_TIMER);
		matchidx = match_regex(domainin, NULL, -1, REGEX_CLI, true);
		if(matchidx == -1)
			logg("    NO MATCH!");
		logg("   Time: %.3f msec", timer_elapsed_msec(REGEX_TIMER));
	}

	// Return status 0 = MATCH, 1 = ERROR, 2 = NO MATCH
	return matchidx > -1 ? EXIT_SUCCESS : 2;
}
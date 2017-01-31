/* Copyright (c) 2016, Kalray Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <string.h>
#include <stdint.h>
#include <HAL/hal/core/optimize.h>
#include <HAL/hal/core/mp.h>
#include <odp/rpc/rpc.h>
#include <odp/rpc/eth.h>
#include <odp_packet_io_internal.h>
#include "odp_rx_internal.h"

void rx_options_default(rx_opts_t *options)
{
	options->nRx = 0;
	options->rr_policy = -1;
	options->rr_offset = 0;
	options->flow_controlled = 0;
	options->min_rx = -1;
	options->max_rx = -1;
}

int rx_parse_options(const char **str, rx_opts_t *options)
{
	const char* pptr = *str;
	char * eptr;
	if (!strncmp(pptr, "tags=", strlen("tags="))){
		pptr += strlen("tags=");
		options->nRx = strtoul(pptr, &eptr, 10);
		if(pptr == eptr){
			ODP_ERR("Invalid tag count %s\n", pptr);
			return -1;
		}
		pptr = eptr;
	} else if (!strncmp(pptr, "rrpolicy=", strlen("rrpolicy="))){
		pptr += strlen("rrpolicy=");
		options->rr_policy = strtoul(pptr, &eptr, 10);
		if(pptr == eptr){
			ODP_ERR("Invalid rrpolicy %s\n", pptr);
			return -1;
		}
		pptr = eptr;
	} else if (!strncmp(pptr, "rroffset=", strlen("rroffset="))){
		pptr += strlen("rroffset=");
		options->rr_offset = strtoul(pptr, &eptr, 10);
		if(pptr == eptr){
			ODP_ERR("Invalid rroffset %s\n", pptr);
			return -1;
		}
		pptr = eptr;
	} else if (!strncmp(pptr, "fc=", strlen("fc="))){
		pptr += strlen("fc=");
		options->flow_controlled = strtoul(pptr, &eptr, 10);
		if(pptr == eptr){
			ODP_ERR("Invalid fc %s\n", pptr);
			return -1;
		}
		pptr = eptr;
	} else 	if (!strncmp(pptr, "min_rx=", strlen("min_rx="))){
		pptr += strlen("min_rx=");
		options->min_rx = strtoul(pptr, &eptr, 10);
		if(pptr == eptr){
			ODP_ERR("Invalid min_rx %s\n", pptr);
			return -1;
		}
		pptr = eptr;
	} else if (!strncmp(pptr, "max_rx=", strlen("max_rx="))){
		pptr += strlen("max_rx=");
		options->max_rx = strtoul(pptr, &eptr, 10);
		if(pptr == eptr){
			ODP_ERR("Invalid max_rx %s\n", pptr);
			return -1;
		}
		pptr = eptr;
	} else {
		/* Not a rx_thread option */
		return 0;
	}
	*str = pptr;
	return 1;
}

#define PARSE_HASH_ERR(msg) do { error_msg = msg; goto error_parse; } while (0) ;

static int update_entry(pkt_rule_entry_t *entry) {
	if ( entry->cmp_mask != 0 ) {
		int upper_byte = 7 - (__k1_clz(entry->cmp_mask) - ( 32 - 8 ));
		union {
			uint64_t d;
			uint8_t b[8];
		} original_value, reordered_value;
		reordered_value.d = 0ULL;
		original_value.d = entry->cmp_value;
		for ( int src = upper_byte, dst = 0; src >= 0; --src, ++dst ) {
			reordered_value.b[dst] = original_value.b[src];
		}
		uint64_t bitmask = 0ULL;
		uint8_t cmp_mask = entry->cmp_mask;
		while ( cmp_mask ) {
			int byte_id = __k1_ctz(cmp_mask);
			cmp_mask &= ~( 1 << byte_id );
			bitmask |= 0xffULL << (byte_id * 8);
		}
		if ( reordered_value.d & ~bitmask ) {
			return 1;
		}
		entry->cmp_value = reordered_value.d;
	}
	return 0;
}

const char* parse_hashpolicy(const char* pptr, int *nb_rules,
			     struct pkt_rule *rules, int max_rules) {
	const char *start_ptr = pptr;
	int rule_id = -1;
	int entry_id = 0;
	bool opened_rule = false;
	bool opened_entry = false;
	const char *error_msg;
	char *eptr;

	while ( true ) {
		switch ( *pptr ) {
			case PKT_RULE_OPEN_SIGN:
				if ( opened_rule == true )
					PARSE_HASH_ERR("open rule");
				rule_id++;
				if ( rule_id >= max_rules )
					PARSE_HASH_ERR("too many rules provided");
				entry_id = -1;
				opened_rule = true;
				pptr++;
				break;
			case PKT_RULE_PRIO_SIGN:
				if ( !( opened_rule == true &&
					opened_entry == false &&
					entry_id == -1 ) )
					PARSE_HASH_ERR("misplaced priority sign");
				pptr++;
				int priority = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad priority value");
				if ( priority & ~0x7 )
					PARSE_HASH_ERR("priority must be in [0..7] range");
				rules[rule_id].priority = priority;
				pptr = eptr;
				break;
			case PKT_ENTRY_OPEN_SIGN:
				if ( opened_entry == true || opened_rule == false)
					PARSE_HASH_ERR("open entry");
				entry_id++;
				if ( entry_id > 8 )
					PARSE_HASH_ERR("nb entries > 9");
				opened_entry = true;
				pptr++;
				break;
			case PKT_RULE_CLOSE_SIGN:
				if ( opened_entry == true || opened_rule == false )
					PARSE_HASH_ERR("close rule");
				opened_rule = false;
				pptr++;
				break;
			case PKT_ENTRY_CLOSE_SIGN:
				if ( opened_entry == false || opened_rule == false)
					PARSE_HASH_ERR("close entry");
				opened_entry = false;
				rules[rule_id].nb_entries = entry_id + 1;
				if ( update_entry(&rules[rule_id].entries[entry_id]) )
					PARSE_HASH_ERR("compare value and mask does not fit");
				pptr++;
				break;
			case PKT_ENTRY_OFFSET_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("offset entry");
				pptr++;
				int offset = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad offset");
				rules[rule_id].entries[entry_id].offset = offset;
				pptr = eptr;
				break;
			case PKT_ENTRY_CMP_MASK_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("cmp_mask entry");
				pptr++;
				int cmp_mask = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad comparison mask");
				if ( cmp_mask & ~0xff )
					PARSE_HASH_ERR("cmp mask must be on 8 bits");
				rules[rule_id].entries[entry_id].cmp_mask = cmp_mask;
				pptr = eptr;
				break;
			case PKT_ENTRY_CMP_VALUE_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("cmp_value entry");
				pptr++;
				uint64_t cmp_value = strtoull(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad comparison mask");
				rules[rule_id].entries[entry_id].cmp_value = cmp_value;
				pptr = eptr;
				break;
			case PKT_ENTRY_HASH_MASK_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("hash_mask entry");
				pptr++;
				int hash_mask = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad hash mask");
				if ( hash_mask & ~0xff )
					PARSE_HASH_ERR("hash mask must be on 8 bits");
				rules[rule_id].entries[entry_id].hash_mask = hash_mask;
				pptr = eptr;
				break;
			case ':':
			case '\0':
				if ( opened_entry == true || opened_rule == true )
					PARSE_HASH_ERR("should not end");
				goto end;
			default:
				PARSE_HASH_ERR("unexpected character");
		}
	}

end:
	for ( int _rule_id = 0; _rule_id <= rule_id; ++_rule_id ) {
		for ( int _entry_id = 0; _entry_id < rules[_rule_id].nb_entries; ++_entry_id) {
			if ( rules[_rule_id].entries[_entry_id].cmp_mask == 0 &&
					rules[_rule_id].entries[_entry_id].cmp_value != 0 ) {
				ODP_ERR("rule %d entry %d: "
								"mask 0x%02x value %016llx\n"
								"compare value can't be set when compare mask is 0\n",
								_rule_id, _entry_id,
								rules[_rule_id].entries[_entry_id].cmp_mask,
								rules[_rule_id].entries[_entry_id].cmp_value );
				*nb_rules = 0;
				return NULL;
			}
			ODP_DBG("Rule[%d] (P%d) Entry[%d]: offset %d cmp_mask 0x%x cmp_value %"PRIu64" hash_mask 0x%x> ",
					_rule_id,
					rules[_rule_id].priority,
					entry_id,
					rules[_rule_id].entries[_entry_id].offset,
					rules[_rule_id].entries[_entry_id].cmp_mask,
					rules[_rule_id].entries[_entry_id].cmp_value,
					rules[_rule_id].entries[_entry_id].hash_mask);
		}
	}

	*nb_rules = rule_id + 1;
	return pptr;

error_parse: ;
	int error_index = pptr - start_ptr;
	while( *pptr != ':' && *pptr != '\0' ) {
		pptr++;
	}
	char *error_str = strndup(start_ptr, pptr - start_ptr + 1);
	ODP_ASSERT( error_str != NULL );
	ODP_ERR("Error in parsing hashpolicy: %s\n", error_msg);
	ODP_ERR("%s\n", error_str);
	ODP_ERR("%*s%s\n", error_index, " ", "^");
	free(error_str);
	*nb_rules = 0;
	return NULL;
}

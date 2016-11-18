#include <odp/api/hints.h>
#include <odp/api/std_types.h>
#include <odp/api/traffic_mngr.h>
#include <string.h>

void odp_tm_requirements_init(odp_tm_requirements_t *requirements)
{
	memset(requirements, 0, sizeof(odp_tm_requirements_t));
}

void odp_tm_egress_init(odp_tm_egress_t *egress)
{
	memset(egress, 0, sizeof(odp_tm_egress_t));
}


int odp_tm_capabilities(odp_tm_capabilities_t capabilities[] ODP_UNUSED,
			uint32_t              capabilities_size)
{
	if (capabilities_size == 0)
		return -1;

	return 0;
}

odp_tm_t odp_tm_create(const char            *name ODP_UNUSED,
		       odp_tm_requirements_t *requirements ODP_UNUSED,
		       odp_tm_egress_t       *egress ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

odp_tm_t odp_tm_find(const char            *name ODP_UNUSED,
		     odp_tm_requirements_t *requirements ODP_UNUSED,
		     odp_tm_egress_t       *egress ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

int odp_tm_capability(odp_tm_t odp_tm ODP_UNUSED,
		      odp_tm_capabilities_t *capabilities)
{
	memset(capabilities, 0, sizeof(*capabilities));
	return 0;
}

int odp_tm_destroy(odp_tm_t odp_tm ODP_UNUSED)
{
	return -1;
}

int odp_tm_vlan_marking(odp_tm_t           odp_tm ODP_UNUSED,
			odp_packet_color_t color ODP_UNUSED,
			odp_bool_t         drop_eligible_enabled ODP_UNUSED)
{
	return -1;
}

int odp_tm_ecn_marking(odp_tm_t           odp_tm ODP_UNUSED,
		       odp_packet_color_t color ODP_UNUSED,
		       odp_bool_t         ecn_ce_enabled ODP_UNUSED)
{
	return -1;
}

int odp_tm_drop_prec_marking(odp_tm_t           odp_tm ODP_UNUSED,
			     odp_packet_color_t color ODP_UNUSED,
			     odp_bool_t         drop_prec_enabled ODP_UNUSED)
{
	return -1;
}

void odp_tm_shaper_params_init(odp_tm_shaper_params_t *params ODP_UNUSED)
{
	memset(params, 0, sizeof(*params));
}

odp_tm_shaper_t odp_tm_shaper_create(const char *name ODP_UNUSED,
				     odp_tm_shaper_params_t *params ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

int odp_tm_shaper_destroy(odp_tm_shaper_t shaper_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_shaper_params_read(odp_tm_shaper_t shaper_profile ODP_UNUSED,
			      odp_tm_shaper_params_t *params ODP_UNUSED)
{
	return -1;
}

int odp_tm_shaper_params_update(odp_tm_shaper_t shaper_profile ODP_UNUSED,
				odp_tm_shaper_params_t *params ODP_UNUSED)
{
	return -1;
}

odp_tm_shaper_t odp_tm_shaper_lookup(const char *name ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

void odp_tm_sched_params_init(odp_tm_sched_params_t *params ODP_UNUSED)
{
	memset(params, 0, sizeof(*params));
}

odp_tm_sched_t odp_tm_sched_create(const char *name ODP_UNUSED,
				   odp_tm_sched_params_t *params ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

int odp_tm_sched_destroy(odp_tm_sched_t sched_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_sched_params_read(odp_tm_sched_t sched_profile ODP_UNUSED,
			     odp_tm_sched_params_t *params ODP_UNUSED)
{
	return -1;
}

int odp_tm_sched_params_update(odp_tm_sched_t sched_profile ODP_UNUSED,
			       odp_tm_sched_params_t *params ODP_UNUSED)
{
	return -1;
}

odp_tm_sched_t odp_tm_sched_lookup(const char *name ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

void odp_tm_threshold_params_init(odp_tm_threshold_params_t *params ODP_UNUSED)
{
	memset(params, 0, sizeof(*params));
}

odp_tm_threshold_t odp_tm_threshold_create(const char *name ODP_UNUSED,
					   odp_tm_threshold_params_t *params ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

int odp_tm_threshold_destroy(odp_tm_threshold_t threshold_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_thresholds_params_read(odp_tm_threshold_t threshold_profile ODP_UNUSED,
				  odp_tm_threshold_params_t *params ODP_UNUSED)
{
	return -1;
}

int odp_tm_thresholds_params_update(odp_tm_threshold_t threshold_profile ODP_UNUSED,
				    odp_tm_threshold_params_t *params ODP_UNUSED)
{
	return -1;
}

odp_tm_threshold_t odp_tm_thresholds_lookup(const char *name ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

void odp_tm_wred_params_init(odp_tm_wred_params_t *params ODP_UNUSED)
{
	memset(params, 0, sizeof(*params));
}

odp_tm_wred_t odp_tm_wred_create(const char *name ODP_UNUSED,
				 odp_tm_wred_params_t *params ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

int odp_tm_wred_destroy(odp_tm_wred_t wred_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_wred_params_read(odp_tm_wred_t wred_profile ODP_UNUSED,
			    odp_tm_wred_params_t *params ODP_UNUSED)
{
	return -1;
}

int odp_tm_wred_params_update(odp_tm_wred_t wred_profile ODP_UNUSED,
			      odp_tm_wred_params_t *params ODP_UNUSED)
{
	return -1;
}

odp_tm_wred_t odp_tm_wred_lookup(const char *name ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

void odp_tm_node_params_init(odp_tm_node_params_t *params ODP_UNUSED)
{
	memset(params, 0, sizeof(*params));
}

odp_tm_node_t odp_tm_node_create(odp_tm_t              odp_tm ODP_UNUSED,
				 const char           *name ODP_UNUSED,
				 odp_tm_node_params_t *params ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

int odp_tm_node_destroy(odp_tm_node_t tm_node ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_shaper_config(odp_tm_node_t tm_node ODP_UNUSED,
			      odp_tm_shaper_t shaper_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_sched_config(odp_tm_node_t tm_node ODP_UNUSED,
			     odp_tm_node_t tm_fan_in_node ODP_UNUSED,
			     odp_tm_sched_t sched_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_threshold_config(odp_tm_node_t tm_node ODP_UNUSED,
				 odp_tm_threshold_t thresholds_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_wred_config(odp_tm_node_t tm_node ODP_UNUSED,
			    odp_packet_color_t pkt_color ODP_UNUSED,
			    odp_tm_wred_t wred_profile ODP_UNUSED)
{
	return -1;
}

odp_tm_node_t odp_tm_node_lookup(odp_tm_t odp_tm ODP_UNUSED,
				 const char *name ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

void *odp_tm_node_context(odp_tm_node_t tm_node ODP_UNUSED)
{
	return NULL;
}

int odp_tm_node_context_set(odp_tm_node_t tm_node ODP_UNUSED,
			    void *user_context ODP_UNUSED)
{
	return -1;
}

void odp_tm_queue_params_init(odp_tm_queue_params_t *params ODP_UNUSED)
{
	memset(params, 0, sizeof(*params));
}

odp_tm_queue_t odp_tm_queue_create(odp_tm_t odp_tm ODP_UNUSED,
				   odp_tm_queue_params_t *params ODP_UNUSED)
{
	return ODP_TM_INVALID;
}

int odp_tm_queue_destroy(odp_tm_queue_t tm_queue ODP_UNUSED)
{
	return -1;
}

void *odp_tm_queue_context(odp_tm_queue_t tm_queue ODP_UNUSED)
{
	return NULL;
}

int odp_tm_queue_context_set(odp_tm_queue_t tm_queue ODP_UNUSED,
			     void *user_context ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_shaper_config(odp_tm_queue_t tm_queue ODP_UNUSED,
			       odp_tm_shaper_t shaper_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_sched_config(odp_tm_node_t tm_node ODP_UNUSED,
			      odp_tm_queue_t tm_fan_in_queue ODP_UNUSED,
			      odp_tm_sched_t sched_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_threshold_config(odp_tm_queue_t tm_queue ODP_UNUSED,
				  odp_tm_threshold_t thresholds_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_wred_config(odp_tm_queue_t tm_queue ODP_UNUSED,
			     odp_packet_color_t pkt_color ODP_UNUSED,
			     odp_tm_wred_t wred_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_connect(odp_tm_node_t src_tm_node ODP_UNUSED,
			odp_tm_node_t dst_tm_node ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_disconnect(odp_tm_node_t src_tm_node ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_connect(odp_tm_queue_t tm_queue ODP_UNUSED,
			 odp_tm_node_t dst_tm_node ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_disconnect(odp_tm_queue_t tm_queue ODP_UNUSED)
{
	return -1;
}

int odp_tm_enq(odp_tm_queue_t tm_queue ODP_UNUSED,
	       odp_packet_t pkt ODP_UNUSED)
{
	return -1;
}

int odp_tm_enq_with_cnt(odp_tm_queue_t tm_queue ODP_UNUSED,
			odp_packet_t pkt ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_info(odp_tm_node_t tm_node ODP_UNUSED,
		     odp_tm_node_info_t *info ODP_UNUSED)
{
	return -1;
}

int odp_tm_node_fanin_info(odp_tm_node_t tm_node ODP_UNUSED,
			   odp_tm_node_fanin_info_t *info ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_info(odp_tm_queue_t tm_queue ODP_UNUSED,
		      odp_tm_queue_info_t *info ODP_UNUSED)
{
	return -1;
}

int odp_tm_queue_query(odp_tm_queue_t       tm_queue ODP_UNUSED,
		       uint32_t             query_flags ODP_UNUSED,
		       odp_tm_query_info_t *info ODP_UNUSED)
{
	return -1;
}

int odp_tm_priority_query(odp_tm_t             odp_tm ODP_UNUSED,
			  uint8_t              priority ODP_UNUSED,
			  uint32_t             query_flags ODP_UNUSED,
			  odp_tm_query_info_t *info ODP_UNUSED)
{
	return -1;
}

int odp_tm_total_query(odp_tm_t             odp_tm ODP_UNUSED,
		       uint32_t             query_flags ODP_UNUSED,
		       odp_tm_query_info_t *info ODP_UNUSED)
{
	return -1;
}

int odp_tm_priority_threshold_config(odp_tm_t           odp_tm ODP_UNUSED,
				     uint8_t            priority ODP_UNUSED,
				     odp_tm_threshold_t thresholds_profile ODP_UNUSED)
{
	return -1;
}

int odp_tm_total_threshold_config(odp_tm_t odp_tm ODP_UNUSED,
				  odp_tm_threshold_t thresholds_profile ODP_UNUSED)
{
	return -1;
}

odp_bool_t odp_tm_is_idle(odp_tm_t odp_tm ODP_UNUSED)
{
	return true;
}

/** The odp_tm_stats_print function is used to write implementation-defined
 * information about the specified TM system to the ODP log. The intended use
 * is for debugging.
 *
 * @param[in] odp_tm  Specifies the TM system.
 */
void odp_tm_stats_print(odp_tm_t odp_tm ODP_UNUSED)
{
	return;
}

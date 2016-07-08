/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/packet_io.h>
#include <odp_packet_io_internal.h>
#include <odp_packet_io_queue.h>
#include <odp/packet.h>
#include <odp_packet_internal.h>
#include <odp_internal.h>
#include <odp/spinlock.h>
#include <odp/rwlock.h>
#include <odp/shared_memory.h>
#include <odp/config.h>
#include <odp_queue_internal.h>
#include <odp_schedule_internal.h>
#include <odp_classification_internal.h>
#include <odp_debug_internal.h>

#include <string.h>
#include <errno.h>

pktio_table_t pktio_tbl;

int odp_pktio_init_global(void)
{
	char name[ODP_QUEUE_NAME_LEN];
	pktio_entry_t *pktio_entry;
	queue_entry_t *queue_entry;
	odp_queue_t qid;
	int id, pktio_if;

	memset(&pktio_tbl, 0, sizeof(pktio_table_t));

	odp_spinlock_init(&pktio_tbl.lock);

	for (id = 0; id < ODP_CONFIG_PKTIO_ENTRIES; ++id) {
		pktio_entry = &pktio_tbl.entries[id];

		odp_ticketlock_init(&pktio_entry->s.lock);
		odp_spinlock_init(&pktio_entry->s.cls.lock);
		odp_spinlock_init(&pktio_entry->s.cls.l2_cos_table.lock);
		odp_spinlock_init(&pktio_entry->s.cls.l3_cos_table.lock);

		/* Create a default output queue for each pktio resource */
		snprintf(name, sizeof(name), "%i-pktio_outq_default", (int)id);
		name[ODP_QUEUE_NAME_LEN-1] = '\0';

		qid = odp_queue_create(name, ODP_QUEUE_TYPE_PKTOUT, NULL);
		if (qid == ODP_QUEUE_INVALID)
			return -1;
		pktio_entry->s.outq_default = qid;

		queue_entry = queue_to_qentry(qid);
		queue_entry->s.pktout = (odp_pktio_t)pktio_entry;
	}

	for (pktio_if = 0; pktio_if_ops[pktio_if]; ++pktio_if) {
		if (pktio_if_ops[pktio_if]->init)
			if (pktio_if_ops[pktio_if]->init())
				ODP_ERR("failed to initialized pktio type %d",
					pktio_if);
	}

	return 0;
}

int odp_pktio_init_local(void)
{
	return 0;
}

static int is_free(pktio_entry_t *entry)
{
	return (entry->s.taken == 0);
}

static void set_free(pktio_entry_t *entry)
{
	entry->s.taken = 0;
}

static void set_taken(pktio_entry_t *entry)
{
	entry->s.taken = 1;
}

static void lock_entry(pktio_entry_t *entry)
{
	odp_ticketlock_lock(&entry->s.lock);
}

static void unlock_entry(pktio_entry_t *entry)
{
	odp_ticketlock_unlock(&entry->s.lock);
}

static void lock_entry_classifier(pktio_entry_t *entry)
{
	lock_entry(entry);
	odp_spinlock_lock(&entry->s.cls.lock);
}

static void unlock_entry_classifier(pktio_entry_t *entry)
{
	odp_spinlock_unlock(&entry->s.cls.lock);
	unlock_entry(entry);
}

static void init_pktio_entry(pktio_entry_t *entry)
{
	set_taken(entry);
	pktio_cls_enabled_set(entry, 0);
	entry->s.inq_default = ODP_QUEUE_INVALID;

	pktio_classifier_init(entry);
}

static odp_pktio_t alloc_lock_pktio_entry(void)
{
	pktio_entry_t *entry;
	int i;

	for (i = 0; i < ODP_CONFIG_PKTIO_ENTRIES; ++i) {
		entry = &pktio_tbl.entries[i];
		if (is_free(entry)) {
			lock_entry_classifier(entry);
			if (is_free(entry)) {
				init_pktio_entry(entry);
				return (odp_pktio_t)entry;
			}
			unlock_entry_classifier(entry);
		}
	}

	return ODP_PKTIO_INVALID;
}

static int free_pktio_entry(odp_pktio_t id)
{
	pktio_entry_t *entry = get_pktio_entry(id);

	if (entry == NULL)
		return -1;

	set_free(entry);

	return 0;
}

static odp_pktio_t setup_pktio_entry(const char *dev, odp_pool_t pool,
				     const odp_pktio_param_t *param)
{
	odp_pktio_t id;
	pktio_entry_t *pktio_entry;
	int ret = -1;
	int pktio_if;

	if (strlen(dev) >= PKTIO_NAME_LEN - 1) {
		/* ioctl names limitation */
		ODP_ERR("pktio name %s is too big, limit is %d bytes\n",
			dev, PKTIO_NAME_LEN - 1);
		return ODP_PKTIO_INVALID;
	}

	id = alloc_lock_pktio_entry();
	if (id == ODP_PKTIO_INVALID) {
		ODP_ERR("No resources available.\n");
		return ODP_PKTIO_INVALID;
	}
	/* if successful, alloc_pktio_entry() returns with the entry locked */

	pktio_entry = get_pktio_entry(id);
	if (!pktio_entry)
		return ODP_PKTIO_INVALID;

	memcpy(&pktio_entry->s.param, param, sizeof(odp_pktio_param_t));

	for (pktio_if = 0; pktio_if_ops[pktio_if]; ++pktio_if) {
		ret = pktio_if_ops[pktio_if]->open(id, pktio_entry, dev, pool);

		if (!ret) {
			pktio_entry->s.ops = pktio_if_ops[pktio_if];
			break;
		}
	}

	if (ret != 0) {
		unlock_entry_classifier(pktio_entry);
		free_pktio_entry(id);
		id = ODP_PKTIO_INVALID;
		ODP_ERR("Unable to init any I/O type.\n");
	} else {
		snprintf(pktio_entry->s.name,
			 sizeof(pktio_entry->s.name), "%s", dev);
		pktio_entry->s.state = STATE_STOP;
		unlock_entry_classifier(pktio_entry);
	}

	pktio_entry->s.handle = id;

	return id;
}

static int pool_type_is_packet(odp_pool_t pool)
{
	odp_pool_info_t pool_info;

	if (pool == ODP_POOL_INVALID)
		return 0;

	if (odp_pool_info(pool, &pool_info) != 0)
		return 0;

	return pool_info.params.type == ODP_POOL_PACKET;
}

odp_pktio_t odp_pktio_open(const char *dev, odp_pool_t pool,
			   const odp_pktio_param_t *param)
{
	odp_pktio_t id;

	ODP_ASSERT(pool_type_is_packet(pool));

	id = odp_pktio_lookup(dev);
	if (id != ODP_PKTIO_INVALID) {
		/* interface is already open */
		__odp_errno = EEXIST;
		return ODP_PKTIO_INVALID;
	}

	odp_spinlock_lock(&pktio_tbl.lock);
	id = setup_pktio_entry(dev, pool, param);
	odp_spinlock_unlock(&pktio_tbl.lock);

	return id;
}

static int _pktio_close(pktio_entry_t *entry)
{
	int ret;

	ret = entry->s.ops->close(entry);
	if(ret)
		return -1;

	set_free(entry);

	return 0;
}

int odp_pktio_close(odp_pktio_t id)
{
	pktio_entry_t *entry;
	int res;

	entry = get_pktio_entry(id);
	if (entry == NULL)
		return -1;

	lock_entry(entry);
	if (!is_free(entry)){
		res = _pktio_close(entry);
		if (res)
			ODP_ABORT("unable to close pktio\n");
	}
	unlock_entry(entry);

	if (res != 0)
		return -1;

	return 0;
}

int odp_pktio_start(odp_pktio_t id)
{
	pktio_entry_t *entry;
	int res = 0;

	entry = get_pktio_entry(id);
	if (!entry)
		return -1;

	lock_entry(entry);
	if (entry->s.state == STATE_START) {
		unlock_entry(entry);
		return -1;
	}
	if (entry->s.ops->start)
		res = entry->s.ops->start(entry);
	if (!res)
		entry->s.state = STATE_START;
	unlock_entry(entry);

	return res;
}

static int _pktio_stop(pktio_entry_t *entry)
{
	int res = 0;

	if (entry->s.state == STATE_STOP)
		return -1;

	if (entry->s.ops->stop)
		res = entry->s.ops->stop(entry);
	if (!res)
		entry->s.state = STATE_STOP;

	return res;
}

int odp_pktio_stop(odp_pktio_t id)
{
	pktio_entry_t *entry;
	int res = 0;


	entry = get_pktio_entry(id);
	if (!entry)
		return -1;

	lock_entry(entry);
	res = _pktio_stop(entry);
	unlock_entry(entry);

	return res;
}

odp_pktio_t odp_pktio_lookup(const char *dev)
{
	odp_pktio_t pktio = ODP_PKTIO_INVALID;
	pktio_entry_t *entry;
	int i;

	odp_spinlock_lock(&pktio_tbl.lock);

	for (i = 0; i < ODP_CONFIG_PKTIO_ENTRIES; ++i) {
		entry = &pktio_tbl.entries[i];
		if (!entry || is_free(entry))
			continue;

		lock_entry(entry);

		if (!is_free(entry) &&
		    strncmp(entry->s.name, dev, PKTIO_NAME_LEN) == 0)
			pktio = (odp_pktio_t) entry;

		unlock_entry(entry);

		if (pktio != ODP_PKTIO_INVALID)
			break;
	}

	odp_spinlock_unlock(&pktio_tbl.lock);

	return pktio;
}

int odp_pktio_recv(odp_pktio_t id, odp_packet_t pkt_table[], int len)
{
	pktio_entry_t *pktio_entry = get_pktio_entry(id);
	int pkts;
	int i;

	if (pktio_entry == NULL)
		return -1;

	if (pktio_entry->s.state == STATE_STOP ||
	    pktio_entry->s.param.in_mode == ODP_PKTIN_MODE_DISABLED) {
		__odp_errno = EPERM;
		return -1;
	}

	pkts = pktio_entry->s.ops->recv(pktio_entry, pkt_table, len);

	if (pkts < 0)
		return pkts;

	for (i = 0; i < pkts; ++i)
		odp_packet_hdr(pkt_table[i])->input = id;

	return pkts;
}

int odp_pktio_send(odp_pktio_t id, odp_packet_t pkt_table[], int len)
{
	pktio_entry_t *pktio_entry = get_pktio_entry(id);
	int pkts;

	if (pktio_entry == NULL)
		return -1;

	if (pktio_entry->s.state == STATE_STOP ||
	    pktio_entry->s.param.out_mode == ODP_PKTOUT_MODE_DISABLED) {
		__odp_errno = EPERM;
		return -1;
	}

	pkts = pktio_entry->s.ops->send(pktio_entry, pkt_table, len);

	return pkts;
}

int odp_pktio_inq_setdef(odp_pktio_t id, odp_queue_t queue)
{
	pktio_entry_t *pktio_entry = get_pktio_entry(id);
	queue_entry_t *qentry;

	if (pktio_entry == NULL || queue == ODP_QUEUE_INVALID)
		return -1;

	qentry = queue_to_qentry(queue);

	if (qentry->s.type != ODP_QUEUE_TYPE_PKTIN)
		return -1;

	lock_entry(pktio_entry);
	if (pktio_entry->s.state != STATE_STOP) {
		unlock_entry(pktio_entry);
		return -1;
	}
	pktio_entry->s.inq_default = queue;
	unlock_entry(pktio_entry);

	switch (qentry->s.type) {
	/* Change to ODP_QUEUE_TYPE_POLL when ODP_QUEUE_TYPE_PKTIN is removed */
	case ODP_QUEUE_TYPE_PKTIN:
		/* User polls the input queue */
		queue_lock(qentry);
		qentry->s.pktin = id;
		queue_unlock(qentry);

	/* Uncomment when ODP_QUEUE_TYPE_PKTIN is removed
		break;
	case ODP_QUEUE_TYPE_SCHED:
	*/
		/* Packet input through the scheduler */
		if (schedule_pktio_start(id, ODP_SCHED_PRIO_LOWEST)) {
			ODP_ERR("Schedule pktio start failed\n");
			return -1;
		}
		break;
	default:
		ODP_ABORT("Bad queue type\n");
	}

	return 0;
}

int odp_pktio_inq_remdef(odp_pktio_t id)
{
	pktio_entry_t *pktio_entry = get_pktio_entry(id);
	odp_queue_t queue;
	queue_entry_t *qentry;

	if (pktio_entry == NULL)
		return -1;

	lock_entry(pktio_entry);
	if (pktio_entry->s.state != STATE_STOP) {
		unlock_entry(pktio_entry);
		return -1;
	}
	queue = pktio_entry->s.inq_default;
	qentry = queue_to_qentry(queue);

	queue_lock(qentry);
	qentry->s.pktin = ODP_PKTIO_INVALID;
	queue_unlock(qentry);

	pktio_entry->s.inq_default = ODP_QUEUE_INVALID;
	unlock_entry(pktio_entry);

	return 0;
}

odp_queue_t odp_pktio_inq_getdef(odp_pktio_t id)
{
	pktio_entry_t *pktio_entry = get_pktio_entry(id);

	if (pktio_entry == NULL)
		return ODP_QUEUE_INVALID;

	return pktio_entry->s.inq_default;
}

odp_queue_t odp_pktio_outq_getdef(odp_pktio_t id)
{
	pktio_entry_t *pktio_entry = get_pktio_entry(id);

	if (pktio_entry == NULL)
		return ODP_QUEUE_INVALID;

	return pktio_entry->s.outq_default;
}

int pktout_enqueue(queue_entry_t *qentry, odp_buffer_hdr_t *buf_hdr)
{
	odp_packet_t pkt = (odp_packet_t)(buf_hdr);
	int len = 1;
	int nbr;

	nbr = odp_pktio_send(qentry->s.pktout, &pkt, len);
	return (nbr == len ? 0 : -1);
}

odp_buffer_hdr_t *pktout_dequeue(queue_entry_t *qentry ODP_UNUSED)
{
	ODP_ABORT("attempted dequeue from a pktout queue");
	return NULL;
}

int pktout_enq_multi(queue_entry_t *qentry, odp_buffer_hdr_t *buf_hdr[],
		     int num)
{
	int nbr;
	nbr = odp_pktio_send(qentry->s.pktout, (odp_packet_t*)buf_hdr, num);
	return nbr;
}

int pktout_deq_multi(queue_entry_t *qentry ODP_UNUSED,
		     odp_buffer_hdr_t *buf_hdr[] ODP_UNUSED,
		     int num ODP_UNUSED)
{
	ODP_ABORT("attempted dequeue from a pktout queue");
	return 0;
}

int pktin_enqueue(queue_entry_t *qentry ODP_UNUSED,
		  odp_buffer_hdr_t *buf_hdr ODP_UNUSED, int sustain ODP_UNUSED)
{
	ODP_ABORT("attempted enqueue to a pktin queue");
	return -1;
}


int pktin_enq_multi(queue_entry_t *qentry ODP_UNUSED,
		    odp_buffer_hdr_t *buf_hdr[] ODP_UNUSED,
		    int num ODP_UNUSED, int sustain ODP_UNUSED)
{
	ODP_ABORT("attempted enqueue to a pktin queue");
	return 0;
}

int pktin_deq_multi(queue_entry_t *qentry, odp_buffer_hdr_t *buf_hdr[], int num)
{
	int nbr;
	odp_packet_t pkt_tbl[QUEUE_MULTI_MAX];
	int pkts, j;
	odp_pktio_t pktio;

	nbr = queue_deq_multi(qentry, buf_hdr, num);
	if (odp_unlikely(nbr > num))
		ODP_ABORT("queue_deq_multi req: %d, returned %d\n",
			num, nbr);

	/** queue already has number of requsted buffers,
	 *  do not do receive in that case.
	 */
	if (nbr == num)
		return nbr;

	pktio = qentry->s.pktin;

	pkts = odp_pktio_recv(pktio, pkt_tbl, QUEUE_MULTI_MAX);
	if (pkts <= 0)
		return nbr;

	if(nbr + pkts <= num) {
		j = 0;
	} else {
		j = (pkts + nbr) - num;
		queue_enq_multi(qentry,
				(odp_buffer_hdr_t**)&pkt_tbl[num - nbr],
				j, 0);
	}
	memcpy(&buf_hdr[nbr], &pkt_tbl[0], (pkts - j) * sizeof(*pkt_tbl));
	nbr += pkts - j;

	return nbr;
}
odp_buffer_hdr_t *pktin_dequeue(queue_entry_t *qentry)
{
	odp_buffer_hdr_t * hdr = NULL;

	pktin_deq_multi(qentry, &hdr, 1);
	return hdr;
}

int pktin_poll(pktio_entry_t *entry)
{
	odp_packet_t pkt_tbl[QUEUE_MULTI_MAX];
	int num;

	if (odp_unlikely(is_free(entry)))
		return -1;

	if (odp_unlikely(entry->s.inq_default == ODP_QUEUE_INVALID))
		return -1;

	if (entry->s.state == STATE_STOP)
		return 0;

	num = odp_pktio_recv(entry->s.handle, pkt_tbl, QUEUE_MULTI_MAX);

	if (num == 0)
		return 0;

	if (num < 0) {
		ODP_ERR("Packet recv error\n");
		return -1;
	}

	queue_entry_t *qentry;
	qentry = queue_to_qentry(entry->s.inq_default);
	queue_enq_multi(qentry, (odp_buffer_hdr_t**)pkt_tbl, num, 0);
	return 0;
}

int odp_pktio_mtu(odp_pktio_t id)
{
	pktio_entry_t *entry;
	int ret;

	entry = get_pktio_entry(id);
	if (entry == NULL) {
		ODP_DBG("pktio entry %d does not exist\n", id);
		return -1;
	}

	if (odp_unlikely(is_free(entry))) {
		ODP_DBG("already freed pktio\n");
		return -1;
	}
	ret = entry->s.ops->mtu_get(entry);

	return ret;
}

int odp_pktio_promisc_mode_set(odp_pktio_t id, odp_bool_t enable)
{
	pktio_entry_t *entry;
	int ret;

	entry = get_pktio_entry(id);
	if (entry == NULL) {
		ODP_DBG("pktio entry %d does not exist\n", id);
		return -1;
	}

	lock_entry(entry);

	if (odp_unlikely(is_free(entry))) {
		unlock_entry(entry);
		ODP_DBG("already freed pktio\n");
		return -1;
	}
	if (entry->s.state != STATE_STOP) {
		unlock_entry(entry);
		return -1;
	}

	ret = entry->s.ops->promisc_mode_set(entry, enable);

	unlock_entry(entry);
	return ret;
}

int odp_pktio_promisc_mode(odp_pktio_t id)
{
	pktio_entry_t *entry;
	int ret;

	entry = get_pktio_entry(id);
	if (entry == NULL) {
		ODP_DBG("pktio entry %d does not exist\n", id);
		return -1;
	}

	if (odp_unlikely(is_free(entry))) {
		ODP_DBG("already freed pktio\n");
		return -1;
	}

	ret = entry->s.ops->promisc_mode_get(entry);

	return ret;
}

int odp_pktio_mac_addr(odp_pktio_t id, void *mac_addr, int addr_size)
{
	pktio_entry_t *entry;
	int ret = ETH_ALEN;

	if (addr_size < ETH_ALEN) {
		/* Output buffer too small */
		return -1;
	}

	entry = get_pktio_entry(id);
	if (entry == NULL) {
		ODP_DBG("pktio entry %d does not exist\n", id);
		return -1;
	}

	if (odp_unlikely(is_free(entry))) {
		ODP_DBG("already freed pktio\n");
		return -1;
	}

	ret = entry->s.ops->mac_get(entry, mac_addr);

	return ret;
}

void odp_pktio_param_init(odp_pktio_param_t *params)
{
	memset(params, 0, sizeof(odp_pktio_param_t));
}

int odp_pktio_term_global(void)
{
	pktio_entry_t *pktio_entry;
	int ret = 0;
	int id;
	int pktio_if;

	for (id = 0; id < ODP_CONFIG_PKTIO_ENTRIES; ++id) {
		pktio_entry = &pktio_tbl.entries[id];
		ret = odp_queue_destroy(pktio_entry->s.outq_default);
		if (ret)
			ODP_ABORT("unable to destroy outq %s\n",
				  pktio_entry->s.name);

		if (is_free(pktio_entry))
			continue;

		lock_entry(pktio_entry);
		if (pktio_entry->s.state != STATE_STOP) {
			ret = _pktio_stop(pktio_entry);
			if (ret)
				ODP_ABORT("unable to stop pktio %s\n",
					  pktio_entry->s.name);
		}
		ret = _pktio_close(pktio_entry);
		if (ret)
			ODP_ABORT("unable to close pktio %s\n",
				  pktio_entry->s.name);
               unlock_entry(pktio_entry);
	}

	for (pktio_if = 0; pktio_if_ops[pktio_if]; ++pktio_if) {
		if (pktio_if_ops[pktio_if]->term)
			if (pktio_if_ops[pktio_if]->term())
				ODP_ERR("failed to terminate pktio type %d",
					pktio_if);
	}

	return ret;
}

int _odp_pktio_stats(odp_pktio_t pktio,
		     _odp_pktio_stats_t *stats)
{
	pktio_entry_t *entry;
	int ret = -1;

	entry = get_pktio_entry(pktio);
	if (entry == NULL) {
		ODP_DBG("pktio entry %d does not exist\n", pktio);
		return -1;
	}

	lock_entry(entry);

	if (odp_unlikely(is_free(entry))) {
		unlock_entry(entry);
		ODP_DBG("already freed pktio\n");
		return -1;
	}

	if (entry->s.ops->stats)
		ret = entry->s.ops->stats(entry, stats);
	else
		memset(stats, 0, sizeof(*stats));
	unlock_entry(entry);
	return ret;
}

void _odp_pktio_stats_print(odp_pktio_t pktio,
			    const _odp_pktio_stats_t *stats)
{
	pktio_entry_t *entry;

	entry = get_pktio_entry(pktio);
	printf("PKTIO stats: '%s'\n"
	       "\tInOctets    : %6llu\n"
	       "\tInUcastPkts : %6llu\n"
	       "\tInDiscards  : %6llu\n"
	       "\tInDropped   : %6llu\n"
	       "\tInErrors    : %6llu\n"
	       "\tInUnknwnProt: %6llu\n"
	       "\tOutOctets   : %6llu\n"
	       "\tOutUcastPkts: %6llu\n"
	       "\tOutDiscards : %6llu\n"
	       "\tOutErrors   : %6llu\n",
	       entry->s.name,
	       stats->in_octets,
	       stats->in_ucast_pkts,
	       stats->in_discards,
	       stats->in_dropped,
	       stats->in_errors,
	       stats->in_unknown_protos,
	       stats->out_octets,
	       stats->out_ucast_pkts,
	       stats->out_discards,
	       stats->out_errors);
}

void odp_pktio_print(odp_pktio_t id)
{
	pktio_entry_t *entry;
	uint8_t addr[ETH_ALEN];
	int max_len = 512;
	char str[max_len];
	int len = 0;
	int n = max_len - 1;

	entry = get_pktio_entry(id);
	if (entry == NULL) {
		ODP_DBG("pktio entry %d does not exist\n", id);
		return;
	}
	INVALIDATE(entry);
	len += snprintf(&str[len], n - len,
			"pktio\n");
	len += snprintf(&str[len], n - len,
			"  handle       %" PRIu64 "\n", odp_pktio_to_u64(id));
	len += snprintf(&str[len], n - len,
			"  name         %s\n", entry->s.name);
	len += snprintf(&str[len], n - len,
			"  state        %s\n",
			entry->s.state ==  STATE_START ? "start" :
		       (entry->s.state ==  STATE_STOP ? "stop" : "unknown"));
	memset(addr, 0, sizeof(addr));
	odp_pktio_mac_addr(id, addr, ETH_ALEN);
	len += snprintf(&str[len], n - len,
			"  mac          %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	len += snprintf(&str[len], n - len,
			"  mtu          %d\n", odp_pktio_mtu(id));
	len += snprintf(&str[len], n - len,
			"  promisc      %s\n",
			odp_pktio_promisc_mode(id) ? "yes" : "no");
	str[len] = '\0';

	ODP_PRINT("\n%s\n", str);
}

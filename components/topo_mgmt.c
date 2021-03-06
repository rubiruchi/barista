/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \ingroup compnt
 * @{
 * \defgroup topo_mgmt Topology Management
 * \brief (Management) topology management
 * @{
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 * \author Yeonkeun Kim <yeonk@kaist.ac.kr>
 */

#include "topo_mgmt.h"

/** \brief Topology management ID */
#define TOPO_MGMT_ID 3034593885

/////////////////////////////////////////////////////////////////////

/** \brief The structure of a topology */
typedef struct _topo_t {
    uint64_t dpid; /**< Datapath ID */
    uint32_t remote; /**< Remote switch */

    port_t port[__MAX_NUM_PORTS]; /**< Ports */
} topo_t;

/** \brief Network topology */
topo_t *topo;

/** \brief The number of links */
int num_links;

/** \brief Lock for topology updates */
pthread_rwlock_t topo_lock;

/////////////////////////////////////////////////////////////////////

/**
 * \brief Function to send a LLDP packet using a pktout message
 * \param dpid Datapath ID
 * \param port Output port
 */
static int send_lldp(uint64_t dpid, port_t *port)
{
    pktout_t pktout = {0};

    pktout.dpid = dpid;
    pktout.port = -1;

    switch_t sw = {0};
    sw.dpid = dpid;
    ev_sw_get_xid(TOPO_MGMT_ID, &sw);

    pktout.xid = sw.xid;
    pktout.buffer_id = -1;

    pktout.total_len = 46;

    // LLDP
    lldp_chassis_id *chassis;  // mandatory
    lldp_port_id *portid;      // mandatory
    lldp_ttl *ttl;             // mandatory
    lldp_system_desc *desc;    // optional
    lldp_eol *eol;             // mandatory

    uint8_t multi[] = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e};
    uint8_t mac[ETH_ALEN] = { *((uint8_t *)&dpid + 5),
                              *((uint8_t *)&dpid + 4),
                              *((uint8_t *)&dpid + 3),
                              *((uint8_t *)&dpid + 2),
                              *((uint8_t *)&dpid + 1),
                              *((uint8_t *)&dpid + 0) };

    struct ether_header *eth_header = (struct ether_header *)pktout.data; // 14

    memmove(eth_header->ether_shost, mac, ETH_ALEN);
    memmove(eth_header->ether_dhost, multi, ETH_ALEN);

    eth_header->ether_type = htons(ETHERTYPE_LLDP); // 32

    // lldp_chassis_id tlv
    chassis = (lldp_chassis_id *)(eth_header + 1);
    chassis->hdr.type = LLDP_TLVT_CHASSISID;
    chassis->hdr.length = 7;
    chassis->subtype = LLDP_CHASSISID_SUBTYPE_MACADDR;

    memmove(chassis->data, &port->hw_addr, ETH_ALEN);

    // lldp_port_id tlv
    portid = (lldp_port_id *)((uint8_t *)chassis + chassis->hdr.length + 2);
    portid->hdr.type = LLDP_TLVT_PORTID;
    portid->hdr.length = 5;
    portid->subtype = LLDP_PORTID_SUBTYPE_COMPONENT;

    uint32_t port_data = htonl(port->port);
    memmove(portid->data, &port_data, 4);

    // lldp_time_to_live tlv
    ttl = (lldp_ttl *)((uint8_t *)portid + portid->hdr.length + 2);
    ttl->hdr.type = LLDP_TLVT_TTL;
    ttl->hdr.length = 2;
    ttl->ttl = htons(DEFAULT_TTL);

    // lldp_port_description tlv
    desc = (lldp_system_desc *)((uint8_t *)ttl + ttl->hdr.length + 2);
    desc->hdr.type = LLDP_TLVT_SYSTEM_DESC;
    desc->hdr.length = 8;

    uint64_t net_dpid = htonll(dpid);
    memmove(desc->data, &net_dpid, 8);

    // lldp_end_of_lldpdu tlv
    eol = (lldp_eol *)((uint8_t *)desc + desc->hdr.length + 2);
    eol->hdr.type = LLDP_TLVT_EOL;
    eol->hdr.length = 0;

    // flip bytes
    chassis->hdr.raw = htons(chassis->hdr.raw);
    portid->hdr.raw = htons(portid->hdr.raw);
    ttl->hdr.raw = htons(ttl->hdr.raw);
    desc->hdr.raw = htons(desc->hdr.raw);
    eol->hdr.raw = htons(eol->hdr.raw);

    // output
    pktout.num_actions = 1;
    pktout.action[0].type = ACTION_OUTPUT;
    pktout.action[0].port = port->port;

    ev_dp_send_packet(TOPO_MGMT_ID, &pktout);

    return 0;
}

/////////////////////////////////////////////////////////////////////

/**
 * \brief Function to insert a new link
 * \param src_dpid Source datapath ID
 * \param src_port Source port
 * \param dst_dpid Destination datapath ID
 * \param dst_port Destination port
 */
static int insert_link(uint64_t src_dpid, uint16_t src_port, uint64_t dst_dpid, uint16_t dst_port)
{
    if (src_dpid == 0 || src_port == 0 || dst_dpid == 0 || dst_port == 0)
        return 0;

    pthread_rwlock_rdlock(&topo_lock);

    int i;
    for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
        if (topo[i].dpid == src_dpid) { // check source dpid
            if (topo[i].port[src_port].port == src_port) { // check source port
                if (topo[i].port[src_port].next_dpid == 0 && topo[i].port[src_port].next_port == 0) {
                    pthread_rwlock_unlock(&topo_lock);

                    pthread_rwlock_wrlock(&topo_lock);

                    topo[i].port[src_port].next_dpid = dst_dpid;
                    topo[i].port[src_port].next_port = dst_port;

                    num_links++;

                    pthread_rwlock_unlock(&topo_lock);

                    port_t link = {0};

                    link.dpid = src_dpid;
                    link.port = src_port;
                    link.next_dpid = dst_dpid;
                    link.next_port = dst_port;

                    ev_link_added(TOPO_MGMT_ID, &link);

                    return 1;
                } else if (topo[i].port[src_port].next_dpid == dst_dpid && topo[i].port[src_port].next_port == dst_port) {
                    break;
                } else {
                    DEBUG("Inconsistent link {(%lu, %u) -> (%lu, %u)} -> {(%lu, %u) -> (%lu, %u)}\n",
                          src_dpid, src_port, topo[i].port[src_port].next_dpid, topo[i].port[src_port].next_port,
                          src_dpid, src_port, dst_dpid, dst_port);
                    break;
                }
            }
        }
    }

    pthread_rwlock_unlock(&topo_lock);

    return 0;
}

/////////////////////////////////////////////////////////////////////

/**
 * \brief The main function
 * \param activated The activation flag of this component
 * \param argc The number of arguments
 * \param argv Arguments
 */
int topo_mgmt_main(int *activated, int argc, char **argv)
{
    LOG_INFO(TOPO_MGMT_ID, "Init - Topology management");

    num_links = 0;

    topo = (topo_t *)CALLOC(__DEFAULT_TABLE_SIZE, sizeof(topo_t));
    if (topo == NULL) {
        LOG_ERROR(TOPO_MGMT_ID, "calloc() error");
        return -1;
    }

    pthread_rwlock_init(&topo_lock, NULL);

    activate();

    while (*activated) {
        pthread_rwlock_rdlock(&topo_lock);

        int i, j;
        for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
            if (topo[i].dpid > 0) { // check source dpid
                if (topo[i].remote == TRUE) // check location
                    continue;

                for (j=0; j<__MAX_NUM_PORTS; j++) {
                    if (topo[i].port[j].port > 0) { // check source port
                        send_lldp(topo[i].dpid, &topo[i].port[j]);
                    }
                }
            }
        }

        pthread_rwlock_unlock(&topo_lock);

        for (i=0; i<TOPO_MGMT_REQUEST_TIME; i++) {
            if (!*activated) break;
            else waitsec(1, 0);
        }
    }

    return 0;
}

/**
 * \brief The cleanup function
 * \param activated The activation flag of this component
 */
int topo_mgmt_cleanup(int *activated)
{
    LOG_INFO(TOPO_MGMT_ID, "Clean up - Topology management");

    deactivate();

    pthread_rwlock_destroy(&topo_lock);

    FREE(topo);

    return 0;
}

/**
 * \brief Function to list up all detected links
 * \param cli The pointer of the Barista CLI
 */
static int topo_listup(cli_t *cli)
{
    pthread_rwlock_rdlock(&topo_lock);

    cli_print(cli, "<Link List>");
    cli_print(cli, "  The total number of links: %d", num_links);

    int i, j, cnt = 0;
    for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
        if (topo[i].dpid > 0) { // check source dpid
            for (j=0; j<__MAX_NUM_PORTS; j++) {
                if (topo[i].port[j].port > 0) { // check source port
                    if (topo[i].port[j].next_port > 0) { // check destination port
                        cli_print(cli, "  Link #%d (%s): {(%lu, %u) -> (%lu, %u)}, rx_pkts: %lu, rx_bytes: %lu, "
                                       "tx_pkts: %lu, tx_bytes: %lu", ++cnt, (topo[i].remote == TRUE) ? "Remote" : "Local",
                                  topo[i].dpid, topo[i].port[j].port, topo[i].port[j].next_dpid, topo[i].port[j].next_port,
                                  topo[i].port[j].rx_packets, topo[i].port[j].rx_bytes,
                                  topo[i].port[j].tx_packets, topo[i].port[j].tx_bytes);
                    }
                }
            }
        }
    }

    pthread_rwlock_unlock(&topo_lock);

    return 0;
}

/**
 * \brief Function to print all links connected to a switch
 * \param cli The pointer of the Barista CLI
 * \param dpid_str Datapath ID
 */
static int topo_showup(cli_t *cli, char *dpid_str)
{
    uint64_t dpid = strtoull(dpid_str, NULL, 0);

    cli_print(cli, "<Link List [%s]>", dpid_str);

    pthread_rwlock_rdlock(&topo_lock);

    int i, j, cnt = 0;
    for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
        if (topo[i].dpid == dpid) { // check source dpid
            for (j=0; j<__MAX_NUM_PORTS; j++) {
                if (topo[i].port[j].port > 0) { // check source port
                    if (topo[i].port[j].next_port > 0) { // check destination port
                        cli_print(cli, "  Link #%d (%s): {(%lu, %u) -> (%lu, %u)}, rx_pkts: %lu, rx_bytes: %lu, "
                                       "tx_pkts: %lu, tx_bytes: %lu", ++cnt, (topo[i].remote == TRUE) ? "Remote" : "Local",
                                  topo[i].dpid, topo[i].port[j].port, topo[i].port[j].next_dpid, topo[i].port[j].next_port,
                                  topo[i].port[j].rx_packets, topo[i].port[j].rx_bytes, 
                                  topo[i].port[j].tx_packets, topo[i].port[j].tx_bytes);
                    }
                }
            }
            break;
        }
    }

    pthread_rwlock_unlock(&topo_lock);

    return 0;
}

/**
 * \brief The CLI function
 * \param cli The pointer of the Barista CLI
 * \param args Arguments
 */
int topo_mgmt_cli(cli_t *cli, char **args)
{
    if (args[0] != NULL && strcmp(args[0], "list") == 0 && args[1] != NULL && strcmp(args[1], "links") == 0 && args[2] == NULL) {
        topo_listup(cli);
        return 0;
    } else if (args[0] != NULL && strcmp(args[0], "show") == 0 && args[1] != NULL && args[2] == NULL) {
        topo_showup(cli, args[1]);
        return 0;
    }

    cli_print(cli, "<Available Commands>");
    cli_print(cli, "  topo_mgmt list links");
    cli_print(cli, "  topo_mgmt show [datapath ID]");

    return 0;
}

/**
 * \brief The handler function
 * \param ev Read-only event
 * \param ev_out Read-write event (if this component has the write permission)
 */
int topo_mgmt_handler(const event_t *ev, event_out_t *ev_out)
{
#ifdef __ENABLE_CBENCH
    return 0;
#endif /* __ENABLE_CBENCH */

    switch (ev->type) {
    case EV_DP_RECEIVE_PACKET:
        PRINT_EV("EV_DP_RECEIVE_PACKET\n");
        {
            const pktin_t *pktin = ev->pktin;
            struct ether_header *eth_header = (struct ether_header *)pktin->data;

            if (pktin->proto & PROTO_LLDP) {
                uint64_t src_dpid = 0, dst_dpid = pktin->dpid;
                uint32_t src_port = 0, dst_port = pktin->port;

                struct tlv_header *tlv = (struct tlv_header *)(eth_header + 1);
                tlv->raw = ntohs(tlv->raw);
                while (tlv->type != LLDP_TLVT_EOL) {
                    switch (tlv->type) {
                    case LLDP_TLVT_PORTID:
                        {
                            lldp_port_id *port = (lldp_port_id *)tlv;
                            switch (port->subtype) {
                            case LLDP_PORTID_SUBTYPE_COMPONENT:
                                memmove(&src_port, port->data, 4);
                                src_port = ntohl(src_port);
                                break;
                            default:
                                break;
                            }
                        }
                        break;
                    case LLDP_TLVT_SYSTEM_DESC:
                        {
                            lldp_system_desc *desc = (lldp_system_desc *)tlv;
                            memmove(&src_dpid, desc->data, 8);
                            src_dpid = ntohll(src_dpid);
                        }
                        break;
                    default:
                        break;
                    }

                    // move to next tlv
                    tlv = (struct tlv_header *)((uint8_t *)tlv + sizeof(struct tlv_header) + tlv->length);
                    tlv->raw = ntohs(tlv->raw);
                }

                if (insert_link(src_dpid, src_port, dst_dpid, dst_port)) {
                    LOG_INFO(TOPO_MGMT_ID, "Detected a new link {(%lu, %u) -> (%lu, %u)}", src_dpid, src_port, dst_dpid, dst_port);
                }

                return -1;
            }
        }
        break;
    case EV_SW_CONNECTED:
        PRINT_EV("EV_SW_CONNECTED\n");
        {
            const switch_t *sw = ev->sw;

            pthread_rwlock_wrlock(&topo_lock);

            int i;
            for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
                if (topo[i].dpid == 0) {
                    topo[i].dpid = sw->dpid;
                    topo[i].remote = sw->remote;
                    break;
                }
            }

            pthread_rwlock_unlock(&topo_lock);
        }
        break;
    case EV_SW_DISCONNECTED:
        PRINT_EV("EV_SW_DISCONNECTED\n");
        {
            const switch_t *sw = ev->sw;

            pthread_rwlock_wrlock(&topo_lock);

            int i, j;
            for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
                if (topo[i].dpid == sw->dpid) { // check source dpid
                    for (j=0; j<__MAX_NUM_PORTS; j++) {
                        if (topo[i].port[j].port > 0) { // check source port
                            if (topo[i].port[j].next_port > 0) { // check destination port
                                LOG_INFO(TOPO_MGMT_ID, "Deleted a link {(%lu, %u) -> (%lu, %u)}", 
                                         topo[i].dpid, topo[i].port[j].port, 
                                         topo[i].port[j].next_dpid, topo[i].port[j].next_port);

                                if (topo[i].remote == FALSE) {
                                    port_t link = {0};

                                    link.dpid = topo[i].dpid;
                                    link.port = topo[i].port[j].port;
                                    link.next_dpid = topo[i].port[j].next_dpid;
                                    link.next_port = topo[i].port[j].next_port;

                                    ev_link_deleted(TOPO_MGMT_ID, &link);
                                }

                                num_links--;
                            }

                            // reset port
                            memset(&topo[i].port[j], 0, sizeof(port_t));
                        }
                    }

                    // reset dpid
                    topo[i].dpid = 0;
                    topo[i].remote = FALSE;

                    break;
                }
            }

            pthread_rwlock_unlock(&topo_lock);
        }
        break;
    case EV_DP_PORT_ADDED:
        PRINT_EV("EV_DP_PORT_ADDED\n");
        {
            const port_t *port = ev->port;

            pthread_rwlock_wrlock(&topo_lock);

            int i;
            for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
                if (topo[i].dpid == port->dpid) { // check source dpid
                    if (port->port > 0) { // check source port
                        topo[i].port[port->port].port = port->port;
                        memmove(&topo[i].port[port->port].hw_addr, port->hw_addr, ETH_ALEN);
                    }

                    break;
                }
            }

            pthread_rwlock_unlock(&topo_lock);
        }
        break;
    case EV_DP_PORT_DELETED:
        PRINT_EV("EV_DP_PORT_DELETED\n");
        {
            const port_t *port = ev->port;

            pthread_rwlock_wrlock(&topo_lock);

            int i;
            for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
                if (topo[i].dpid == port->dpid) { // check source dpid
                    if (topo[i].port[port->port].port == port->port) { // check source port
                        if (topo[i].port[port->port].next_port > 0) { // check destination port
                            LOG_INFO(TOPO_MGMT_ID, "Deleted a link {(%lu, %u) -> (%lu, %u)}",
                                     topo[i].dpid, topo[i].port[port->port].port,
                                     topo[i].port[port->port].next_dpid, topo[i].port[port->port].next_port);

                            if (topo[i].remote == FALSE) {
                                port_t link = {0};

                                link.dpid = port->dpid;
                                link.port = port->port;
                                link.next_dpid = topo[i].port[port->port].next_dpid;
                                link.next_port = topo[i].port[port->port].next_port;

                                ev_link_deleted(TOPO_MGMT_ID, &link);
                            }

                            num_links--;
                        }

                        memset(&topo[i].port[port->port], 0, sizeof(port_t));

                        break;
                    }
                }
            }

            pthread_rwlock_unlock(&topo_lock);
        }
        break;
    case EV_DP_PORT_STATS:
        PRINT_EV("EV_DP_PORT_STATS\n");
        {
            const port_t *port = ev->port;

            pthread_rwlock_wrlock(&topo_lock);

            int i;
            for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
                if (topo[i].dpid == port->dpid) { // check source dpid
                    if (topo[i].port[port->port].port == 0) { // check source port
                        if (topo[i].remote == FALSE) {
                            topo[i].port[port->port].port = port->port;
                        }

                        break;
                    } else {
                        topo[i].port[port->port].rx_packets = port->rx_packets - topo[i].port[port->port].old_rx_packets;
                        topo[i].port[port->port].rx_bytes = port->rx_bytes - topo[i].port[port->port].old_rx_bytes;
                        topo[i].port[port->port].tx_packets = port->tx_packets - topo[i].port[port->port].old_tx_packets;
                        topo[i].port[port->port].tx_bytes = port->tx_bytes - topo[i].port[port->port].old_tx_bytes;

                        topo[i].port[port->port].old_rx_packets = port->rx_packets;
                        topo[i].port[port->port].old_rx_bytes = port->rx_bytes;
                        topo[i].port[port->port].old_tx_packets = port->tx_packets;
                        topo[i].port[port->port].old_tx_bytes = port->tx_bytes;

                        DEBUG("[%lu, %u] - rx_pkts: %lu, rx_bytes: %lu, tx_pkts: %lu, tx_bytes: %lu\n",
                               port->dpid, port->port,
                               topo[i].port[port->port].rx_packets, topo[i].port[port->port].rx_bytes,
                               topo[i].port[port->port].tx_packets, topo[i].port[port->port].tx_bytes);

                        break;
                    }
                }
            }

            pthread_rwlock_unlock(&topo_lock);
        }
        break;
    case EV_LINK_ADDED:
        PRINT_EV("EV_LINK_ADDED\n");
        {
            const port_t *link = ev->port;

            if (link->remote == FALSE) break;

            pthread_rwlock_wrlock(&topo_lock);

            int i;
            for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
                if (topo[i].dpid == link->dpid) { // check source dpid
                    if (topo[i].port[link->port].port == 0) { // check source port
                        topo[i].port[link->port].port = link->port;

                        topo[i].port[link->port].next_dpid = link->next_dpid;
                        topo[i].port[link->port].next_port = link->next_port;

                        num_links++;

                        LOG_INFO(TOPO_MGMT_ID, "Detected a new link {(%lu, %u) -> (%lu, %u)}",
                                 link->dpid, link->port, link->next_dpid, link->next_port);
                    }

                    break;
                }
            }

            pthread_rwlock_unlock(&topo_lock);
        }
        break;
    case EV_LINK_DELETED:
        PRINT_EV("EV_LINK_DELETED\n");
        {
            const port_t *link = ev->port;

            if (link->remote == FALSE) break;

            pthread_rwlock_wrlock(&topo_lock);

            int i;
            for (i=0; i<__DEFAULT_TABLE_SIZE; i++) {
                if (topo[i].dpid == link->dpid) { // check source dpid
                    if (topo[i].port[link->port].port == link->port) { // check source port
                        memset(&topo[i].port[link->port], 0, sizeof(port_t));
                        num_links--;

                        LOG_INFO(TOPO_MGMT_ID, "Deleted a link {(%lu, %u) -> (%lu, %u)}",
                                 link->dpid, link->port, link->next_dpid, link->next_port);
                    }

                    break;
                }
            }

            pthread_rwlock_unlock(&topo_lock);
        }
        break;
    default:
        break;
    }

    return 0;
}

/**
 * @}
 *
 * @}
 */

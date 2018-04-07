/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \ingroup app
 * @{
 * \defgroup l2_learning L2 Learning
 * \brief (Network) L2 learning
 * @{
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

#include "l2_learning.h"

/** \brief L2 learning ID */
#define L2_LEARNING_ID 153576300

/////////////////////////////////////////////////////////////////////

/** \brief The size of a MAC table */
#define MAC_HASH_SIZE 8192

/** \brief The structure of a MAC entry */
typedef struct _mac_entry_t {
    uint64_t dpid; /**< Datapath ID */
    uint16_t port; /**< Port */

    uint32_t ip; /**< IP address */
    uint64_t mac; /**< MAC address */

    struct _mac_entry_t *prev; /**< Previous entry */
    struct _mac_entry_t *next; /**< Next entry */

    struct _mac_entry_t *r_next; /**< Next entry for removal */
} mac_entry_t;

/** \brief The structure of a MAC table */
typedef struct _mac_table_t {
    mac_entry_t *head; /**< The head pointer */
    mac_entry_t *tail; /**< The tail pointer */

    pthread_rwlock_t lock; /**< The lock for management */
} mac_table_t;

/** \brief MAC table */
mac_table_t *mac_table;

/////////////////////////////////////////////////////////////////////

#include "mac_queue.h"

/////////////////////////////////////////////////////////////////////

/**
 * \brief Function to conduct an output action into the data plane
 * \param pktin Pktin message
 * \param port Port
 */
static int send_packet(const pktin_t *pktin, uint16_t port)
{
    pktout_t out = {0};

    PKTOUT_INIT(out, pktin);

    out.num_actions = 1;
    out.action[0].type = ACTION_OUTPUT;
    out.action[0].port = port;

    av_dp_send_packet(L2_LEARNING_ID, &out);

    return 0;
}

/**
 * \brief Function to insert a flow rule into the data plane
 * \param pktin Pktin message
 * \param port Port
 */
static int insert_flow(const pktin_t *pktin, uint16_t port)
{
    flow_t out = {0};

    FLOW_INIT(out, pktin);

    out.idle_timeout = DEFAULT_IDLE_TIMEOUT;
    out.hard_timeout = DEFAULT_HARD_TIMEOUT;
    out.priority = DEFAULT_PRIORITY;

    out.num_actions = 1;
    out.action[0].type = ACTION_OUTPUT;
    out.action[0].port = port;

    av_dp_insert_flow(L2_LEARNING_ID, &out);

    return 0;
}

/**
 * \brief Function to conduct a drop action into the data plane
 * \param pktin Pktin message
 */
static int discard_packet(const pktin_t *pktin)
{
    pktout_t out = {0};

    PKTOUT_INIT(out, pktin);

    out.num_actions = 1;
    out.action[0].type = ACTION_DISCARD;

    av_dp_send_packet(L2_LEARNING_ID, &out);

    return 0;
}

/////////////////////////////////////////////////////////////////////

/**
 * \brief Function to handle pktin messages
 * \param pktin Pktin message
 */
static int l2_learning(const pktin_t *pktin)
{
#ifndef __ENABLE_CBENCH
    if ((pktin->proto & (PROTO_ARP | PROTO_IPV4)) == 0) {
        discard_packet(pktin);
        return -1;
    }

    uint64_t mac = mac2int(pktin->src_mac);
    uint32_t mkey = hash_func((uint32_t *)&mac, 2) % MAC_HASH_SIZE;

    pthread_rwlock_rdlock(&mac_table[mkey].lock);

    // check source MAC
    mac_entry_t *curr = mac_table[mkey].head;
    while (curr != NULL) {
        if (curr->dpid == pktin->dpid && curr->mac == mac) {
            break;
        }
        curr = curr->next;
    }

    pthread_rwlock_unlock(&mac_table[mkey].lock);

    // new MAC?
    if (curr == NULL) {
        curr = mac_dequeue();
        if (curr == NULL) {
            discard_packet(pktin);
            return -1;
        }

        curr->dpid = pktin->dpid;
        curr->port = pktin->port;

        curr->ip = pktin->src_ip;
        curr->mac = mac;

        curr->prev = curr->next = NULL;

        pthread_rwlock_wrlock(&mac_table[mkey].lock);

        if (mac_table[mkey].head == NULL) {
            mac_table[mkey].head = curr;
            mac_table[mkey].tail = curr;
        } else {
            curr->prev = mac_table[mkey].tail;
            mac_table[mkey].tail->next = curr;
            mac_table[mkey].tail = curr;
        }

        pthread_rwlock_unlock(&mac_table[mkey].lock);
    }

    // get destination MAC
    mac = mac2int(pktin->dst_mac);

    // broadcast?
    if (mac == 0xffffffffffff) {
        send_packet(pktin, PORT_FLOOD);
        return 0;
    }

    uint16_t port = 0;
    mkey = hash_func((uint32_t *)&mac, 2) % MAC_HASH_SIZE;

    pthread_rwlock_rdlock(&mac_table[mkey].lock);

    // where to forward?
    curr = mac_table[mkey].head;
    while (curr != NULL) {
        if (curr->dpid == pktin->dpid && curr->mac == mac) {
            port = curr->port;
            break;
        }
        curr = curr->next;
    }

    pthread_rwlock_unlock(&mac_table[mkey].lock);

    // unknown port?
    if (port == 0) {
        send_packet(pktin, PORT_FLOOD);
        return 0;
    }

    if (pktin->proto & PROTO_IPV4) // IPv4
        insert_flow(pktin, port);
    else // ARP
        send_packet(pktin, port);

    return 0;
#else /* __ENABLE_CBENCH */
    send_packet(pktin, PORT_FLOOD);
    return 0;
#endif /* __ENABLE_CBENCH */
}

/////////////////////////////////////////////////////////////////////

/**
 * \brief The main function
 * \param activated The activation flag of this application
 * \param argc The number of arguments
 * \param argv Arguments
 */
int l2_learning_main(int *activated, int argc, char **argv)
{
    ALOG_INFO(L2_LEARNING_ID, "Init - L2 learning");

    mac_table = (mac_table_t *)CALLOC(MAC_HASH_SIZE, sizeof(mac_table_t));
    if (mac_table == NULL) {
        PERROR("calloc");
        return -1;
    }

    int i;
    for (i=0; i<MAC_HASH_SIZE; i++) {
        pthread_rwlock_init(&mac_table[i].lock, NULL);
    }

    mac_q_init();

    activate();

    return 0;
}

/**
 * \brief The cleanup function
 * \param activated The activation flag of this application
 */
int l2_learning_cleanup(int *activated)
{
    ALOG_INFO(L2_LEARNING_ID, "Clean up - L2 learning");

    deactivate();

    int i;
    for (i=0; i<MAC_HASH_SIZE; i++) {
        pthread_rwlock_wrlock(&mac_table[i].lock);

        mac_entry_t *curr = mac_table[i].head;
        while (curr != NULL) {
            mac_entry_t *tmp = curr;
            curr = curr->next;
            FREE(tmp);
        }

        pthread_rwlock_unlock(&mac_table[i].lock);
        pthread_rwlock_destroy(&mac_table[i].lock);
    }

    mac_q_destroy();

    FREE(mac_table);

    return 0;
}

/**
 * \brief Function to list up all MAC tables
 * \param cli The CLI pointer
 */
static int list_all_entries(cli_t *cli)
{
    cli_print(cli, "<MAC Tables>");

    int i, cnt = 0;
    for (i=0; i<MAC_HASH_SIZE; i++) {
        pthread_rwlock_rdlock(&mac_table[i].lock);

        mac_entry_t *curr = mac_table[i].head;
        while (curr != NULL) {
            uint8_t mac[ETH_ALEN];
            int2mac(curr->mac, mac);

            char macaddr[__CONF_WORD_LEN] = {0};
            mac2str(mac, macaddr);

            struct in_addr ip_addr;
            ip_addr.s_addr = curr->ip;

            cli_print(cli, "  %lu: %s (%s), %u", curr->dpid, macaddr, inet_ntoa(ip_addr), curr->port);

            cnt++;

            curr = curr->next;
        }

        pthread_rwlock_unlock(&mac_table[i].lock);
    }

    if (!cnt)
        cli_print(cli, "  No entry");

    return 0;
}

/**
 * \brief Function to show the MAC table for a specific switch
 * \param cli The CLI pointer
 * \param dpid_str Datapath ID
 */
static int show_entry_switch(cli_t *cli, char *dpid_str)
{
    uint64_t dpid = strtoull(dpid_str, NULL, 0);

    cli_print(cli, "<MAC Table for Switch [%lu]>", dpid);

    int i, cnt = 0;
    for (i=0; i<MAC_HASH_SIZE; i++) {
        pthread_rwlock_rdlock(&mac_table[i].lock);

        mac_entry_t *curr = mac_table[i].head;
        while (curr != NULL) {
            if (curr->dpid == dpid) {
                uint8_t mac[ETH_ALEN];
                int2mac(curr->mac, mac);

                char macaddr[__CONF_WORD_LEN] = {0};
                mac2str(mac, macaddr);

                struct in_addr ip_addr;
                ip_addr.s_addr = curr->ip;

                cli_print(cli, "  %lu: %s (%s), %u", curr->dpid, macaddr, inet_ntoa(ip_addr), curr->port);

                cnt++;
            }
            curr = curr->next;
        }

        pthread_rwlock_unlock(&mac_table[i].lock);
    }

    if (!cnt)
        cli_print(cli, "  No entry");

    return 0;
}

/**
 * \brief Function to find a MAC entry with a MAC address
 * \param cli The CLI pointer
 * \param macaddr MAC address
 */
static int show_entry_mac(cli_t *cli, const char *macaddr)
{
    uint8_t mac[ETH_ALEN] = {0};

    str2mac(macaddr, mac);
 
    uint64_t imac = mac2int(mac);

    cli_print(cli, "<MAC Entry [%s]>", macaddr);

    int i, cnt = 0;
    for (i=0; i<MAC_HASH_SIZE; i++) {
        pthread_rwlock_rdlock(&mac_table[i].lock);

        mac_entry_t *curr = mac_table[i].head;
        while (curr != NULL) {
            if (curr->mac == imac) {
                struct in_addr ip_addr;
                ip_addr.s_addr = curr->ip;

                cli_print(cli, "  %lu: %s (%s), %u", curr->dpid, macaddr, inet_ntoa(ip_addr), curr->port);

                cnt++;
            }
            curr = curr->next;
        }

        pthread_rwlock_unlock(&mac_table[i].lock);
    }

    if (!cnt)
        cli_print(cli, "  No entry");

    return 0;
}

/**
 * \brief Function to find a MAC entry with an IP address
 * \param cli The CLI pointer
 * \param ipaddr IP address
 */
static int show_entry_ip(cli_t *cli, const char *ipaddr)
{
    struct in_addr ip_addr;
    inet_aton(ipaddr, &ip_addr);

    uint32_t ip = ip_addr.s_addr;

    cli_print(cli, "<MAC Entry [%s]>", ipaddr);

    int i, cnt = 0;
    for (i=0; i<MAC_HASH_SIZE; i++) {
        pthread_rwlock_rdlock(&mac_table[i].lock);

        mac_entry_t *curr = mac_table[i].head;
        while (curr != NULL) {
            if (curr->ip == ip) {
                uint8_t mac[ETH_ALEN];
                int2mac(curr->mac, mac);

                char macaddr[__CONF_WORD_LEN] = {0};
                mac2str(mac, macaddr);

                cli_print(cli, "  %lu: %s (%s), %u", curr->dpid, macaddr, ipaddr, curr->port);

                cnt++;
            }

            curr = curr->next;
        }

        pthread_rwlock_unlock(&mac_table[i].lock);
    }

    if (!cnt)
        cli_print(cli, "  No entry");

    return 0;
}

/**
 * \brief The CLI function
 * \param cli The CLI pointer
 * \param args Arguments
 */
int l2_learning_cli(cli_t *cli, char **args)
{
    if (args[0] != NULL && strcmp(args[0], "list") == 0) {
        if (args[1] != NULL && strcmp(args[1], "tables") == 0 && args[2] == NULL) {
            list_all_entries(cli);
            return 0;
        }
    } else if (args[0] != NULL && strcmp(args[0], "show") == 0) {
        if (args[1] != NULL && strcmp(args[1], "switch") == 0 && args[2] != NULL && args[3] == NULL) {
            show_entry_switch(cli, args[2]);
            return 0;
        } else if (args[1] != NULL && strcmp(args[1], "mac") == 0 && args[2] != NULL && args[3] == NULL) {
            show_entry_mac(cli, args[2]);
            return 0;
        } else if (args[1] != NULL && strcmp(args[1], "ip") == 0 && args[2] != NULL && args[3] == NULL) {
            show_entry_ip(cli, args[2]);
            return 0;
        }
    }

    cli_print(cli, "<Available Commands>");
    cli_print(cli, "  l2_learning list tables");
    cli_print(cli, "  l2_learning show switch [DPID]");
    cli_print(cli, "  l2_learning show mac [MAC address]");
    cli_print(cli, "  l2_learning show ip [IP address]");

    return 0;
}

/**
 * \brief The handler function
 * \param av Read-only app event
 * \param av_out Read-write app event (if this application has the write permission)
 */
int l2_learning_handler(const app_event_t *av, app_event_out_t *av_out)
{
    switch (av->type) {
    case AV_DP_RECEIVE_PACKET:
        PRINT_EV("AV_DP_RECEIVE_PACKET\n");
        {
            const pktin_t *pktin = av->pktin;

            l2_learning(pktin);
        }
        break;
    case AV_DP_PORT_DELETED:
        PRINT_EV("AV_DP_PORT_DELETED\n");
        {
            const port_t *port = av->port;

            int i;
            for (i=0; i<MAC_HASH_SIZE; i++) {
                mac_table_t tmp_list = {0};

                pthread_rwlock_wrlock(&mac_table[i].lock);

                mac_entry_t *curr = mac_table[i].head;
                while (curr != NULL) {
                    if (curr->dpid == port->dpid && curr->port == port->port) {
                        if (tmp_list.head == NULL) {
                            tmp_list.head = curr;
                            tmp_list.tail = curr;
                            curr->r_next = NULL;
                        } else {
                            tmp_list.tail->r_next = curr;
                            tmp_list.tail = curr;
                            curr->r_next = NULL;
                        }
                    }

                    curr = curr->next;
                }

                curr = tmp_list.head;
                while (curr != NULL) {
                    mac_entry_t *tmp = curr;

                    curr = curr->r_next;

                    if (tmp->prev != NULL && tmp->next != NULL) {
                        tmp->prev->next = tmp->next;
                        tmp->next->prev = tmp->prev;
                    } else if (tmp->prev == NULL && tmp->next != NULL) {
                        mac_table[i].head = tmp->next;
                        tmp->next->prev = NULL;
                    } else if (tmp->prev != NULL && tmp->next == NULL) {
                        mac_table[i].tail = tmp->prev;
                        tmp->prev->next = NULL;
                    } else if (tmp->prev == NULL && tmp->next == NULL) {
                        mac_table[i].head = NULL;
                        mac_table[i].tail = NULL;
                    }

                    mac_enqueue(tmp);
                }

                pthread_rwlock_unlock(&mac_table[i].lock);
            }
        }
        break;
    case AV_SW_DISCONNECTED:
        PRINT_EV("AV_SW_DISCONNECTED\n");
        {
            const switch_t *sw = av->sw;

            int i;
            for (i=0; i<MAC_HASH_SIZE; i++) {
                mac_table_t tmp_list = {0};

                pthread_rwlock_wrlock(&mac_table[i].lock);

                mac_entry_t *curr = mac_table[i].head;
                while (curr != NULL) {
                    if (curr->dpid == sw->dpid) {
                        if (tmp_list.head == NULL) {
                            tmp_list.head = curr;
                            tmp_list.tail = curr;
                            curr->r_next = NULL;
                        } else {
                            tmp_list.tail->r_next = curr;
                            tmp_list.tail = curr;
                            curr->r_next = NULL;
                        }
                    }

                    curr = curr->next;
                }

                curr = tmp_list.head;
                while (curr != NULL) {
                    mac_entry_t *tmp = curr;

                    curr = curr->next;

                    if (tmp->prev != NULL && tmp->next != NULL) {
                        tmp->prev->next = tmp->next;
                        tmp->next->prev = tmp->prev;
                    } else if (tmp->prev == NULL && tmp->next != NULL) {
                        mac_table[i].head = tmp->next;
                        tmp->next->prev = NULL;
                    } else if (tmp->prev != NULL && tmp->next == NULL) {
                        mac_table[i].tail = tmp->prev;
                        tmp->prev->next = NULL;
                    } else if (tmp->prev == NULL && tmp->next == NULL) {
                        mac_table[i].head = NULL;
                        mac_table[i].tail = NULL;
                    }

                    mac_enqueue(tmp);
                }

                pthread_rwlock_unlock(&mac_table[i].lock);
            }
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

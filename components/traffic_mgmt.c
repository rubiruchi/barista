/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \ingroup compnt
 * @{
 * \defgroup traffic_mgmt Control Traffic Management
 * \brief (Management) control traffic management
 * @{
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

#include "traffic_mgmt.h"

/** \brief Control traffic management ID */
#define TRAFFIC_MGMT_ID 2206472807

/////////////////////////////////////////////////////////////////////

/** \brief Traffic statistics */
traffic_t traffic;

/** \brief Traffic statistics history */
traffic_t *tr_history;

/** \brief The pointer to record the current traffic statistics */
uint32_t tr_history_ptr;

/** \brief Lock for statistics updates */
pthread_spinlock_t tr_lock;

/////////////////////////////////////////////////////////////////////

/**
 * \brief The main function
 * \param activated The activation flag of this component
 * \param argc The number of arguments
 * \param argv Arguments
 */
int traffic_mgmt_main(int *activated, int argc, char **argv)
{
    LOG_INFO(TRAFFIC_MGMT_ID, "Init - Control channel management");

    tr_history_ptr = 0;
    memset(&traffic, 0, sizeof(traffic_t));

    tr_history = (traffic_t *)CALLOC(TRAFFIC_MGMT_HISTORY, sizeof(traffic_t));
    if (tr_history == NULL) {
        LOG_ERROR(TRAFFIC_MGMT_ID, "calloc() error");
        return -1;
    }

    pthread_spin_init(&tr_lock, PTHREAD_PROCESS_PRIVATE);

    LOG_INFO(TRAFFIC_MGMT_ID, "Traffic usage output: %s", DEFAULT_TRAFFIC_FILE);
    LOG_INFO(TRAFFIC_MGMT_ID, "Traffic monitoring period: %d sec", TRAFFIC_MGMT_MONITOR_TIME);

    activate();

    while (*activated) {
        traffic_t tr;

        time_t timer;
        struct tm *tm_info;
        char now[__CONF_WORD_LEN], buf[__CONF_STR_LEN];

        time(&timer);
        tm_info = localtime(&timer);
        strftime(now, __CONF_WORD_LEN-1, "%Y:%m:%d %H:%M:%S", tm_info);

        pthread_spin_lock(&tr_lock);

        memmove(&tr_history[tr_history_ptr++ % TRAFFIC_MGMT_HISTORY], &traffic, sizeof(traffic_t));
        memmove(&tr, &traffic, sizeof(traffic_t));
        memset(&traffic, 0, sizeof(traffic_t));

        pthread_spin_unlock(&tr_lock);

        snprintf(buf, __CONF_STR_LEN-1, "%s - In %lu msgs %lu bytes / Out %lu msgs %lu bytes",
                now, tr.in_pkt_cnt, tr.in_byte_cnt, tr.out_pkt_cnt, tr.out_byte_cnt);

        FILE *fp = fopen(DEFAULT_TRAFFIC_FILE, "a");
        if (fp != NULL) {
            fprintf(fp, "%s\n", buf);
            fclose(fp);
            fp = NULL;
        }

        ev_tr_update_stats(TRAFFIC_MGMT_ID, &tr);

        int i;
        for (i=0; i<TRAFFIC_MGMT_MONITOR_TIME; i++) {
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
int traffic_mgmt_cleanup(int *activated)
{
    LOG_INFO(TRAFFIC_MGMT_ID, "Clean up - Control channel management");

    deactivate();

    pthread_spin_destroy(&tr_lock);

    FREE(tr_history);

    return 0;
}

/**
 * \brief Function to summarize the traffic history for the last n seconds
 * \param cli The pointer of the Barista CLI
 * \param seconds The time range to query
 */
static int traffic_stat_summary(cli_t *cli, char *seconds)
{
    int sec = atoi(seconds);
    if (sec > TRAFFIC_MGMT_HISTORY) sec = TRAFFIC_MGMT_HISTORY;
    else if (sec > tr_history_ptr) sec = tr_history_ptr;

    int ptr = tr_history_ptr - sec;
    if (ptr < 0) ptr = TRAFFIC_MGMT_HISTORY + ptr;

    traffic_t tr = {0};

    int i;
    for (i=0; i<sec; i++) {
        tr.in_pkt_cnt += tr_history[ptr % TRAFFIC_MGMT_HISTORY].in_pkt_cnt;
        tr.in_byte_cnt += tr_history[ptr % TRAFFIC_MGMT_HISTORY].in_byte_cnt;

        tr.out_pkt_cnt += tr_history[ptr % TRAFFIC_MGMT_HISTORY].out_pkt_cnt;
        tr.out_byte_cnt += tr_history[ptr % TRAFFIC_MGMT_HISTORY].out_byte_cnt;

        ptr++;
    }

    double in_pkt_cnt = tr.in_pkt_cnt * 1.0 / sec;
    double in_byte_cnt = tr.in_byte_cnt * 1.0 / sec;

    double out_pkt_cnt = tr.out_pkt_cnt * 1.0 / sec;
    double out_byte_cnt = tr.out_byte_cnt * 1.0 / sec;

    cli_print(cli, "<Traffic Statistics for %d seconds>", sec);
    cli_print(cli, "  - Cumulative values");
    cli_print(cli, "    Inbound packet count : %lu packets", tr.in_pkt_cnt);
    cli_print(cli, "    Inbound byte count   : %lu bytes", tr.in_byte_cnt);
    cli_print(cli, "    Outbound packet count: %lu packets", tr.out_pkt_cnt);
    cli_print(cli, "    Outbound byte count  : %lu bytes", tr.out_byte_cnt);
    cli_print(cli, "  - Average values (per second)");
    cli_print(cli, "    Inbound packet count : %.2f packets", in_pkt_cnt);
    cli_print(cli, "    Inbound byte count   : %.2f bytes", in_byte_cnt);
    cli_print(cli, "    Outbound packet count: %.2f packets", out_pkt_cnt);
    cli_print(cli, "    Outbound byte count  : %.2f bytes", out_byte_cnt);

    return 0;
}

/**
 * \brief The CLI function
 * \param cli The pointer of the Barista CLI
 * \param args Arguments
 */
int traffic_mgmt_cli(cli_t *cli, char **args)
{
    if (args[0] != NULL && strcmp(args[0], "stat") == 0 && args[1] != NULL && args[2] == NULL) {
        traffic_stat_summary(cli, args[1]);
        return 0;
    }

    PRINTF("<Available Commands>\n");
    PRINTF("  traffic_mgmt stat [n secs]\n");

    return 0;
}

/**
 * \brief The handler function
 * \param ev Read-only event
 * \param ev_out Read-write event (if this component has the write permission)
 */
int traffic_mgmt_handler(const event_t *ev, event_out_t *ev_out)
{
    switch (ev->type) {
    case EV_OFP_MSG_IN:
        PRINT_EV("EV_OFP_MSG_IN\n");
        {
            pthread_spin_lock(&tr_lock);

            traffic.in_pkt_cnt++;
            traffic.in_byte_cnt += ev->raw_msg->length;

            pthread_spin_unlock(&tr_lock);
        }
        break;
    case EV_OFP_MSG_OUT:
        PRINT_EV("EV_OFP_MSG_OUT\n");
        {
            pthread_spin_lock(&tr_lock);

            traffic.out_pkt_cnt++;
            traffic.out_byte_cnt += ev->raw_msg->length;

            pthread_spin_unlock(&tr_lock);
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

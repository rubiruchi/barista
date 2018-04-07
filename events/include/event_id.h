/**
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

/** \brief The list of events */
enum {
    EV_NONE,
    // upstream
    EV_OFP_MSG_IN,
    EV_DP_RECEIVE_PACKET,
    EV_DP_FLOW_EXPIRED,
    EV_DP_FLOW_DELETED,
    EV_DP_FLOW_STATS,
    EV_DP_AGGREGATE_STATS,
    EV_DP_PORT_ADDED,
    EV_DP_PORT_MODIFIED,
    EV_DP_PORT_DELETED,
    EV_DP_PORT_STATS,
    EV_ALL_UPSTREAM,
    // downstream
    EV_OFP_MSG_OUT,
    EV_DP_SEND_PACKET,
    EV_DP_INSERT_FLOW,
    EV_DP_MODIFY_FLOW,
    EV_DP_DELETE_FLOW,
    EV_DP_REQUEST_FLOW_STATS,
    EV_DP_REQUEST_AGGREGATE_STATS,
    EV_DP_REQUEST_PORT_STATS,
    EV_ALL_DOWNSTREAM,
    // internal (request-response)
    EV_SW_GET_DPID,
    EV_SW_GET_FD,
    EV_SW_GET_XID,
    EV_SW_GET_VERSION,
    EV_WRT_INTSTREAM,
    // internal (notification)
    EV_SW_NEW_CONN,
    EV_SW_EXPIRED_CONN,
    EV_SW_CONNECTED,
    EV_SW_DISCONNECTED,
    EV_SW_UPDATE_CONFIG,
    EV_SW_UPDATE_DESC,
    EV_HOST_ADDED,
    EV_HOST_DELETED,
    EV_LINK_ADDED,
    EV_LINK_DELETED,
    EV_FLOW_ADDED,
    EV_FLOW_DELETED,
    EV_RS_UPDATE_USAGE,
    EV_TR_UPDATE_STATS,
    EV_LOG_UPDATE_MSGS,
    EV_ALL_INTSTREAM,
    // log
    EV_LOG_DEBUG,
    EV_LOG_INFO,
    EV_LOG_WARN,
    EV_LOG_ERROR,
    EV_LOG_FATAL,
    EV_NUM_EVENTS,
};


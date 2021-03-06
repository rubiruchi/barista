/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

static int ODP_FUNC(odp_t *odp, const ODP_TYPE *data)
{
    int pass = TRUE; // assume that there is no matched policy

    if (odp[0].flag == 0)
        return FALSE; // there is no policy

    int i;
    for (i=0; i<__MAX_POLICIES; i++) {
        if (odp[i].flag == 0) // No more ODP
            break;

        int cnt = 0, match = 0;

        if (odp[i].flag & ODP_DPID) {
            cnt++;

            int j;
            for (j=0; j<__MAX_POLICY_ENTRIES; j++) {
                if (odp[i].dpid[j] == 0) // No more DPID
                    break;

                if (odp[i].dpid[j] == data->dpid) {
                    match++;
                    break;
                }
            }
        }

        if (odp[i].flag & ODP_PORT) {
            cnt++;

            int j;
            for (j=0; j<__MAX_POLICY_ENTRIES; j++) {
                if (odp[i].port[j] == 0) // No more port
                    break;

                if (odp[i].port[j] == data->port) {
                    match++;
                    break;
                }
            }
        }

        if (odp[i].flag & ODP_PROTO) {
            cnt++;

            if (odp[i].proto & data->proto)
                match++;
        } 

        if (odp[i].flag & ODP_VLAN) {
            cnt++;

            int j;
            for (j=0; j<__MAX_POLICY_ENTRIES; j++) {
                if (odp[i].vlan[j] == 0) // No more VLAN
                    break;

                if (odp[i].vlan[j] == data->vlan_id) {
                    match++;
                    break;
                }
            }
        }

        if (odp[i].flag & ODP_SRCIP) {
            cnt++;

            int j;
            for (j=0; j<__MAX_POLICY_ENTRIES; j++) {
                if (odp[i].srcip[j] == 0) // No more src_ip
                    break;

                if ((odp[i].srcip[j] & data->src_ip) == odp[i].srcip[j]) {
                    match++;
                    break;
                }
            }
        }

        if (odp[i].flag & ODP_DSTIP) {
            cnt++;

            int j;
            for (j=0; j<__MAX_POLICY_ENTRIES; j++) {
                if (odp[i].dstip[j] == 0) // No more dst_ip
                    break;

                if ((odp[i].dstip[j] & data->dst_ip) == odp[i].dstip[j]) {
                    match++;
                    break;
                }
            }
        }

        if (odp[i].flag & ODP_SPORT) {
            cnt++;

            int j;
            for (j=0; j<__MAX_POLICY_ENTRIES; j++) {
                if (odp[i].sport[j] == 0) // No more sport
                    break;

                if (odp[i].sport[j] == data->src_port) {
                    match++;
                    break;
                }
            }
        }

        if (odp[i].flag & ODP_DPORT) {
            cnt++;

            int j;
            for (j=0; j<__MAX_POLICY_ENTRIES; j++) {
                if (odp[i].dport[j] == 0) // No more dport
                    break;

                if (odp[i].dport[j] == data->dst_port) {
                    match++;
                    break;
                }
            }
        }

        if (cnt == match) { // found one matched policy
            pass = FALSE;
            break;
        }
    }

    return pass;
}

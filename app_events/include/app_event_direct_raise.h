/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

static int FUNC_NAME(uint32_t id, uint16_t type, uint16_t len, const FUNC_TYPE *data)
{
    int av_num = av_ctx->av_num[type];
    app_t **av_list = av_ctx->av_list[type];

    if (av_list == NULL) return -1;

    app_event_out_t av_out;
    app_event_t *av = (app_event_t *)&av_out;

    av_out.id = id;
    av_out.type = type;
    av_out.length = len;

    av->FUNC_DATA = data;

#ifdef __ENABLE_META_EVENTS
    av_ctx->num_app_events[type]++;
#endif /* __ENABLE_META_EVENTS */

    int i;
    for (i=0; i<av_num; i++) {
        app_t *c = av_list[i];

        if (!c) continue;
        else if (!c->activated) continue; // not activated yet

#ifdef ODP_FUNC
        if (ODP_FUNC(c->odp, data)) continue;
#endif /* ODP_FUNC */

        if (c->perm & APP_WRITE) {
            if (c->site == APP_INTERNAL) { // internal site
#ifdef __ENABLE_META_EVENTS
                c->num_app_events[type]++;
#endif /* __ENABLE_META_EVENTS */

                int ret = c->handler(av, &av_out);
                if (ret && c->perm & APP_EXECUTE) {
                    break;
                }
            } else { // external site
                app_event_out_t *out = &av_out;

#ifdef __ENABLE_META_EVENTS
                c->num_app_events[type]++;
#endif /* __ENABLE_META_EVENTS */

                int ret = av_send_ext_msg(c, id, type, len, data, out->data);
                if (ret && c->perm & APP_EXECUTE) {
                    break;
                }
            }
        } else {
            if (c->site == APP_INTERNAL) { // internal site
#ifdef __ENABLE_META_EVENTS
                c->num_app_events[type]++;
#endif /* __ENABLE_META_EVENTS */

                int ret = c->handler(av, NULL);
                if (ret && c->perm & APP_EXECUTE) {
                    break;
                }
            } else { // external site
                app_event_out_t *out = &av_out;

#ifdef __ENABLE_META_EVENTS
                c->num_app_events[type]++;
#endif /* __ENABLE_META_EVENTS */

                if (c->perm & APP_EXECUTE) {
                    int ret = av_send_ext_msg(c, id, type, len, data, out->data);
                    if (ret) {
                        break;
                    }
                } else {
                    av_push_ext_msg(c, id, type, len, data);
                }
            }
        }
    }

    return 0;
}

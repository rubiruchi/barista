/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \ingroup framework
 * @{
 * \defgroup context Context Management
 * \brief Function to maintain the context of the Barista NOS
 * @{
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

#include "context.h"

/**
 * \brief Function to initialize the context of the Barista NOS
 * \param ctx The context of the Barista NOS
 */
int ctx_init(ctx_t *ctx)
{
    // initialize all fields in a context
    memset(ctx, 0, sizeof(ctx_t));

    return 0;
}

/**
 * @}
 *
 * @}
 */

/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

#pragma once

/** \brief The macro for the main function of a component */
#define DECLARE_MAIN_FUNC(name) \
    extern int name(int *activated, int argc, char **argv)
/** \brief The macro for the handler function of a component */
#define DECLARE_HANDLER_FUNC(name) \
    extern int name(const event_t *ev, event_out_t *ev_out)
/** \brief The macro for the cleanup function of a component */
#define DECLARE_CLEANUP_FUNC(name) \
    extern int name(int *activated)
/** \brief The macro for the CLI function of a component */
#define DECLARE_CLI_FUNC(name) \
    extern int name(cli_t *cli, char **args)

DECLARE_MAIN_FUNC(conflict_main);
DECLARE_HANDLER_FUNC(conflict_handler);
DECLARE_CLEANUP_FUNC(conflict_cleanup);
DECLARE_CLI_FUNC(conflict_cli);

/** \brief The function pointer of a component */
struct _compnt_func_t {
    char *name; /**< Application name */
    compnt_main_f main; /**< The main function pointer */
    compnt_handler_f handler; /**< The handler function pointer */
    compnt_cleanup_f cleanup; /**< The cleanup function pointer */
    compnt_cli_f cli; /**< The CLI function pointer */
} g_components[] = {
    {"conflict", conflict_main, conflict_handler, conflict_cleanup, conflict_cli},
}; /**< The list of function pointers */

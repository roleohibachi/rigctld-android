// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include "rigctld_client.h"
#include "rigctld_stream.h"

int embedded_stream_start(void)
{
    static pthread_once_t client_key = PTHREAD_ONCE_INIT;
    int error = pthread_once(&client_key, rigctld_client_id_init);
    if (error != 0) return -1;
    return rigctld_stream_registry_init(&g_stream_registry);
}

void embedded_stream_set_client(int client_id)
{
    rigctld_client_id_set(client_id);
}

void embedded_stream_close_client(int client_id)
{
    rigctld_stream_registry_close_by_client(&g_stream_registry, client_id);
}

void embedded_stream_stop(void)
{
    rigctld_stream_registry_destroy(&g_stream_registry);
}

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>   // sigaction
#include "vcpCICmdImpl.h"
#include "vcpCIProtocol.h"
#include "vcp_clients.h"
#include "tcpip.h"
#include "rs232.h"

#define IN_RANGE(x, lower_limit, upper_limit) \
    (x >= lower_limit && x <= upper_limit)

#define GET_CLIENT_NAME(client_type) \
   client_type == VCP_CLIENT_TYPE_CONFIGURATOR ? "configurator" : "logger"

static char sendBuff[4096 + 10];
static char recvBuff[4096 + 10];

static vcp_clients_t vcp_clients;

static inline vcp_client_t *get_client(vcp_client_type_t client_type)
{
    int i;

    for (i = 0; i < vcp_clients.num_clients; i++)
    {
        if (vcp_clients.clients[i]->client_type == client_type)
            return vcp_clients.clients[i];
    }
    return NULL;
}

int vcp_clients_add(vcp_client_type_t client_type, vcp_transport_type_t transport_type, 
    void *transport_params)
{
    vcp_client_t *client;
    int ret;

    if (!IN_RANGE(client_type, VCP_CLIENT_TYPE_CONFIGURATOR, VCP_CLIENT_TYPE_LOGGER))
    {
        printf("Error! Invalid value for client_type (%d)\n", client_type);
        return -1;
    }

    if (!IN_RANGE(transport_type, VCP_TRANSPORT_TYPE_TCPIP, VCP_TRANSPORT_TYPE_UART))
    {
        printf("Error! Invalid value for transport_type (%d)\n", transport_type);
        return -1;
    }

    if (vcp_clients.num_clients >= MAX_NUM_VCP_CLIENTS) 
    {
        printf("Error! No more clients can be added\n");
        return -1;
    }

    if (get_client(client_type) != NULL)
    {
        printf("Error! %s already has been added\n", GET_CLIENT_NAME(client_type));
        return -1;
    }

    client = (vcp_client_t *)malloc(sizeof(vcp_client_t));
    if (client == NULL)
        return -1;

    client->client_type = client_type;
    client->transport_type = transport_type;

    vcp_clients.clients[vcp_clients.num_clients++] = client;

    if (client->transport_type == VCP_TRANSPORT_TYPE_TCPIP)
    {
        unsigned short port;
        struct sigaction sa;
        tcpip_server_params_t tcpip_params;

        sa.sa_handler = SIG_IGN;
        sa.sa_flags = 0;
        if (sigemptyset(&sa.sa_mask) == -1 || sigaction(SIGPIPE, &sa, 0) == -1) {
            perror("Failed to ignore SIGPIPE; sigaction\n");
            return -1;
        }

        client->server = tcpip_server_create();
        if (client->server == NULL) 
        {
            printf("Error create UART server for %s.\n", GET_CLIENT_NAME(client->client_type));
            return -1;
        }

        tcpip_params.listenPort = client->client_type == VCP_CLIENT_TYPE_CONFIGURATOR ? 8887 : 8888;

        ret = client->server->open(client->server, GET_CLIENT_NAME(client->client_type), &tcpip_params);
        if (ret != 0)
        {
            printf("Error open TCP/IP server for %s.\n", GET_CLIENT_NAME(client->client_type));
            return -1;
        }
    }
    else // VCP_TRANSPORT_TYPE_UART
    {
        client->server = uart_server_create();
        if (client->server == NULL) 
        {
            printf("Error create UART server for %s.\n", GET_CLIENT_NAME(client->client_type));
            return -1;
        }

        ret = client->server->open(client->server, GET_CLIENT_NAME(client->client_type), transport_params);
        if (ret != 0)
        {
            printf("Error open UART server for %s.\n", GET_CLIENT_NAME(client->client_type));
            return -1;
        }
    }

    if (client->client_type == VCP_CLIENT_TYPE_CONFIGURATOR)
        InitProfiles();

    return 0;
}

int vcp_clients_remove_all()
{
    int i; 

    for (i = 0; i < vcp_clients.num_clients; i++)
    {
        vcp_client_t *client = vcp_clients.clients[i];

        client->server->close(client->server);

        free(client);
    }

    vcp_clients.num_clients = 0;

    return 0;
}

int vcp_clients_preproc()
{
    int n, LenReply;
    vcp_client_t *logger;
    vcp_client_t *configurator;

    if ((logger = get_client(VCP_CLIENT_TYPE_LOGGER)) != NULL)
    {
        if (logger->server->read(logger->server, recvBuff, sizeof(recvBuff) - 1) < 0)
            return -1;
    }

    if ((configurator = get_client(VCP_CLIENT_TYPE_CONFIGURATOR)) != NULL)
    {
        n = configurator->server->read(configurator->server, recvBuff, sizeof(recvBuff) - 1);
        if (n > 0)
        {
            LenReply = VCPCI_GetMessageBuildReply((short *)recvBuff, n, (short *)sendBuff, 0);
            if (LenReply != 0) {
                if (configurator->server->write(configurator->server, sendBuff, LenReply * sizeof(short)) < 0)
                    return -1;
            } else 
                return -1;
        }
        else if (n < 0)
        {
            return -1;
        }
    }

    return 0;
}

int vcp_clients_postproc()
{
    int i;
    char* logb;
    vcp_client_t *logger;

    if ((logger = get_client(VCP_CLIENT_TYPE_LOGGER)) != NULL)
    {
        if (pProfileStruct->p_log != NULL) {
            logb = (char *)vcp_logger_get_buf(VcpReg);   //have to do it: logger mem reinit is inside
            i = vcp_logger_buf_size(VcpReg);
            //transmit logger data if any
            if (i > 0) {
                if (logger->server->write(logger->server, logb, i) < 0)
                    return -1;
            }
        }
    }
    return 0;
}


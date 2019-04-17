#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "tcpip.h"


/* TCP/IP server context */
typedef struct {
    int listenfd;               // server socket descriptor
    int connfd;                 // client socket descriptor
    char client_name[64];       // client name
    unsigned short listenPort;  // server port
} tcpip_server_context_t;


static int tcpip_open(vcp_server_t *server, char *client_name, void *transport_params)
{
    tcpip_server_context_t *context = (tcpip_server_context_t *)server->context;
    tcpip_server_params_t *params = (tcpip_server_params_t *)transport_params;
    struct sockaddr_in serv_addr;
    socklen_t len;

    context->connfd = -1;
    strcpy(context->client_name, client_name);

    context->listenfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    if (setsockopt(context->listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) // to avoid "bind: Address already in use"
    {
        perror("setsockopt");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(params->listenPort); 

    if (bind(context->listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
    {
        // get any port number if the specified port is not available
        serv_addr.sin_port = 0; 
        if (bind(context->listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
        {
            perror("bind");
            return -1;
        }
    }

    if (listen(context->listenfd, 10) < 0) 
    {
        perror("listen");
        return -1;
    } 

    len = sizeof(serv_addr);
    if (getsockname(context->listenfd, (struct sockaddr *)&serv_addr, &len) == -1) // to get used port number
        perror("getsockname");

    context->listenPort = ntohs(serv_addr.sin_port);

    printf("TCP/IP server for %s succesfully created, port is %d\n", context->client_name, context->listenPort);

    return 0;
}

static int tcpip_close(vcp_server_t *server)
{
    tcpip_server_context_t *context = (tcpip_server_context_t *)server->context;

    if (context->connfd != -1) {
        if (close(context->connfd) != 0) {
            perror("close");
            return -1;
        }
        context->connfd = -1;
    }

    if (context->listenfd != -1) {
        if (close(context->listenfd) != 0) {
            perror("close");
            return -1;
        }
        context->listenfd = -1;
    }

    return 0;
}

static int tcpip_read(vcp_server_t *server, void *buf, int len)
{
    tcpip_server_context_t *context = (tcpip_server_context_t *)server->context;
    int n;
    struct sockaddr_in clientname;
    size_t size;

    if (context->connfd == -1)
    {
        size = sizeof(clientname);
        context->connfd = accept(context->listenfd, (struct sockaddr *) &clientname,  &size);
        if (context->connfd < 0)
        {
            if (errno != EWOULDBLOCK)
            {
                perror("accept");
                return -1;
            }
            else
            {
                return 0; // that's not an error
            }
        }

        int one = 1;
        setsockopt(context->connfd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));

        fprintf(stderr,
                "%s server: connect from host %s, port %d.\n",
                context->client_name,
                inet_ntoa(clientname.sin_addr),
                ntohs(clientname.sin_port));

    }

    n = recv(context->connfd, buf, len, MSG_DONTWAIT);

    if (n == -1) 
    { 
        if (errno != EWOULDBLOCK)
        {
            perror("recv");
            return -1;
        }
        else
        {
            return 0; // that's not an error
        }
    } 
    else if (n == 0)
    {
        context->connfd = -1;
        printf("%s server: disconnected!\n", context->client_name);
    }

    return n; 
}

static int tcpip_write(vcp_server_t *server, void *buf, int len)
{
    tcpip_server_context_t *context = (tcpip_server_context_t *)server->context;
    int n; 

    n = send(context->connfd, buf, len, 0);

    return n; 
}

vcp_server_t* tcpip_server_create(void)
{
  vcp_server_t *server = NULL;

  server = (vcp_server_t *)malloc(sizeof(vcp_server_t));
  if (server == NULL)
    return NULL;  

  server->open = tcpip_open;
  server->close = tcpip_close;
  server->write = tcpip_write;
  server->read = tcpip_read;
  
  server->context = (vcp_server_t *)malloc(sizeof(tcpip_server_context_t));
  if (server->context == NULL)
      return NULL;

  return server;
}

void tcpip_server_destroy(vcp_server_t *server)
{
  if (server == NULL)
    return;
  if (server->context != NULL)
      free(server->context);
  free(server);
  server = NULL;
}

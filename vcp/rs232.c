#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rs232.h"


/* UART server context structure */
typedef struct {
    int fd;                     // descriptor
    char client_name[64];       // client name
    int pkt_len;                // current packet length  
    int read_bytes;             
    char buf[4096];             
} uart_server_context_t;

static int set_interface_attribs(int fd, int speed)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int uart_open(vcp_server_t *server, char *client_name, void *transport_params)
{
    int wlen;
    uart_server_context_t *context = (uart_server_context_t *)server->context;
    uart_server_params_t *params = (uart_server_params_t *)transport_params;

    strcpy(context->client_name, client_name);
    context->pkt_len = 0;
    context->read_bytes = 0;

    context->fd = open(params->portname, O_RDWR | O_NOCTTY/* | O_SYNC*/ | O_NONBLOCK);
    if (context->fd < 0) {
        printf("Error opening %s: %s\n", params->portname, strerror(errno));
        return -1;
    }

    if (set_interface_attribs(context->fd, params->speed) != 0)
        return -1;

    printf("UART server for %s succesfully created, port is %s\n", context->client_name, params->portname);

    return 0;
}

int uart_close(vcp_server_t *server)
{
    uart_server_context_t *context = (uart_server_context_t *)server->context;

    close(context->fd);

    return 0;
}

static int uart_write(vcp_server_t *server, void *buf, int len)
{
    int wlen;
    uart_server_context_t *context = (uart_server_context_t *)server->context;

    wlen = write(context->fd, buf, len);
    if (wlen != len) {
        printf("Error from write: %d, %d\n", wlen, errno);
    }

    return 0;
}

static int uart_read(vcp_server_t *server, void *buf, int max_len)
{
    int rdlen, ret;
    uart_server_context_t *context = (uart_server_context_t *)server->context;

    rdlen = read(context->fd, &context->buf[context->read_bytes], sizeof(context->buf) - context->read_bytes);
    if (rdlen > 0) 
    {
        context->read_bytes += rdlen;

        if (context->pkt_len == 0 && context->read_bytes >= 2)
            context->pkt_len = *((short *)context->buf) * sizeof(short);

        if (context->pkt_len != 0 && context->read_bytes >= context->pkt_len)
        {
            memcpy(buf, context->buf, context->pkt_len);
            // copy data for the next packet from the end to the beginning
            memcpy(context->buf, &context->buf[context->pkt_len], context->read_bytes - context->pkt_len);
            context->read_bytes = 0;

            ret = context->pkt_len;
            context->pkt_len = 0;
            return ret;
        }


    } else if (rdlen < 0) {
        printf("Error from read: %d: %s\n", rdlen, strerror(errno));
        return -1;
    }

    return 0;
}

vcp_server_t* uart_server_create(void)
{
  vcp_server_t *server = NULL;
  
  server = (vcp_server_t *)malloc(sizeof(vcp_server_t));
  if (server == NULL)
    return NULL;  

  server->open = uart_open;
  server->close = uart_close;
  server->write = uart_write;
  server->read = uart_read;
  
  server->context = (vcp_server_t *)malloc(sizeof(uart_server_context_t));
  if (server->context == NULL)
      return NULL;

  return server;
}

void uart_server_destroy(vcp_server_t *server)
{
  if (server == NULL)
    return;
  if (server->context != NULL)
      free(server->context);
  free(server);
  server = NULL;
}

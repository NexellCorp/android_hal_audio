#ifndef __RS232_H__
#define __RS232_H__

#include <termios.h>
#include "vcp_clients.h"

/* UART server parameters */
typedef struct {
    char *portname;             // device name, like "/dev/ttymxc1"
    int speed;                  // baudrate. This is should be standard constant from termios.h, like B115200 etc..
} uart_server_params_t;

vcp_server_t* uart_server_create(void);
void uart_server_destroy(vcp_server_t *server);

#endif // __RS232_H__

#ifndef PE_COMMON_H
#define PE_COMMON_H

#define _POSIX_SOURCE
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#define FIFO_EXCHANGE "/tmp/pe_exchange_%d"
#define FIFO_TRADER "/tmp/pe_trader_%d"
#define EX_FIFO_NAME_LEN 25 // len of FIFO_EXCHANGE + id (up to 999999) + null byte
#define TR_FIFO_NAME_LEN 23 // len of FIFO_TRADER + id (up to 999999) + null byte

#define MAX_PROD_NAME 17 // plus null byte
#define ONE_CHAR 2 // one char plus null byte

#define FEE_PERCENTAGE 1
#define MAX_DIGIT 7 // From 999999 plus null byte

// FROM MARKET SELL <PRODUCT> <QTY> <PRICE>; + null byte
// 7+ 5 + 17 + 7 + 7 + 1 = 44
#define MAX_REPLY_LEN 44

// From SELL <ORDER_ID> <PRODUCT> <QTY> <PRICE>; + null byte
// 5 + 7 + 17 + 7 + 7 + 1 = 37
#define MAX_CMD 37

#define DELIM " "

#define MAX_NUM 999999

void sigusr1_handler(int sig, siginfo_t *si, void *ucontext);
int str_2_int(char* line);

#endif

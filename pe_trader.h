#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"

#define BUFFER_SIZE 256

struct message {

    char reply[MAX_REPLY_LEN];
    struct message* next;
    struct message* prev;
};

void print_mssgs(struct message** m_head);

void process_mssgs(struct message** m_head, int* quit, 
    int* order_id, int* tr_fd, int* pending);

void free_queue(struct message** m_head);

void get_responds(struct message** m_head, int* ex_fd);

void connect_2_pipes(int* ex_fd, int* tr_fd, char* id_str);

#endif

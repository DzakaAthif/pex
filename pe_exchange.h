#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"

#define LOG_PREFIX "[PEX]"
#define BUFFER_SIZE 256

struct trader {
    unsigned int id;
    int pid;
    int order_id;
    int dead;
    int print_dead;

    int ex_fifo_fd;
    int tr_fifo_fd;

    char ex_fifo_name[EX_FIFO_NAME_LEN];
    char tr_fifo_name[TR_FIFO_NAME_LEN];

    struct trader* next;
    struct trader* prev;

    struct product* products;
};

struct product {
    long long int num_of_prod;
    long long int money;
    char name[MAX_PROD_NAME];
    struct product* next;
};

struct message {

    char reply[MAX_REPLY_LEN];
    struct trader* trader;
    struct message* next;
    struct message* prev;
};

struct order {
    struct trader* trader;
    int order_id;

    char prod_name[MAX_PROD_NAME];
    int price;
    int qty;

    struct order* next;
    struct order* prev;
};

void process_mssgs(struct message** m_head, 
    struct order** buy_book, struct order** sell_book, 
    struct trader** tr_head, struct product** p_head, 
    long long int* total_fees);

int cancel_cmd(char* cmd, struct trader* trader, 
    struct order** buy_book, struct order** sell_book,
    struct trader** tr_head);

int amend_cmd(char* cmd, struct trader* trader, 
    long long int* total_fees, struct order** buy_book,
    struct order** sell_book, struct trader** tr_head);

struct order* find_order(char** order_type, int* o_type_int,
    struct order*** search_book, struct order*** order_book,
    struct trader* trader, int* order_id,
    struct order** buy_book,
    struct order** sell_book);

int buy_sell_cmd(char* order_type, char* cmd, 
    struct trader* trader, long long int* total_fees, 
    struct order** search_book, struct order** order_book,
    struct trader** tr_head);

void match_buy_sell_order(long long int* total_fees,
    struct order* new_order, struct order** book, 
    int buy);

void put_money_n_product(struct trader* trader, 
    long long int value, int qty, char* prod_name);

void put_order_to_book(struct order* new_order, 
    struct order** book);

int get_buy_sell_info(char* ptr, struct trader* trader, 
    struct order** new_order, int amend);

int check_qty_price(int num, int order_id);

struct order* create_new_order(int* order_id, 
    char* prod_name, int* qty, int* price, 
    struct trader* trader);

void free_order(struct order* target, struct order** book,
    int free_fr);

void print_match(struct order* o_ptr, 
    struct order* new_order, long long int* value, 
    long long int* fee);

void print_traders(struct trader** tr_head);

void print_books(struct order** buy_book, 
    struct order** sell_book, struct product** p_head);

void print_levels(struct order** book, int* levels, 
    char* order_type, char* prod_name);

void print_level(int* new_highest, 
    struct order** book, char* prod_name, 
    char* order_type);

void get_highest_below(struct order** book, 
    int* old_highest, int* new_highest, 
    char* prod_name);

void get_levels(struct order** book, int* levels, 
    char* prod_name);

void send_invalid(struct trader* trader);

void send_fill(struct order* order, int* qty);

void send_market(struct order* order, char* order_type,
    struct trader** tr_head);

void send_reply(struct order* order, char* reply);

void get_responds(struct message** m_head, 
    struct trader** tr_head);

void get_respond(struct message** m_head, struct trader* trader);

int check_is_traders_dead();

void change_dead_trader_status(int* total_traders);

void declare_dead(struct trader* trader);

void cleanup(struct trader** tr_head, struct product** p_head, 
    struct message** m_head, struct order** buy_book, 
    struct order** sell_book);

void free_products(struct product** p_head);

void send_market_open(struct trader** tr_head);

int start_traders(int* argc, char** argv,
    struct trader** tr_head, struct product** p_head, 
    int* total_traders);

void create_trader(int* id, int* pid, int* ex_fd, int* tr_fd, 
    char* ex_fifo, char* tr_fifo, struct trader** tr_head, 
    struct product** p_head);

void copy_products(struct trader* trader, 
    struct product** p_head);

int read_prod_file(char* file_name, int* total_product, 
    struct product** p_head);

int is_there_newline(char* line);

void sigchld_handler(int sig, siginfo_t *si, void *ucontext);

void print_book(struct order** buy_book, 
    struct order** sell_book);

#endif
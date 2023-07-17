#include "pe_exchange.h"

unsigned int responds = 0;
int w_idx = 0;
int r_idx = 0;
int max_queue = 0;
int* responds_queue = NULL;

int trader_died = 0;
int dead_idx = 0;
int* dead_traders = NULL;

struct trader* tr_head = NULL;

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    printf("%s Starting\n", LOG_PREFIX);

    // register signal handlers

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = sigusr1_handler;
    sa.sa_flags = SA_SIGINFO; /* Important. */

	sigaction(SIGUSR1, &sa, NULL);

    struct sigaction sa2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_sigaction = sigchld_handler;
    sa2.sa_flags = SA_SIGINFO; /* Important. */

	sigaction(SIGCHLD, &sa2, NULL);

    signal(SIGPIPE, SIG_IGN);

    // Read product file.
    int total_prod = -1;
    struct product* p_head = NULL;

    int ret = 0;
    ret = read_prod_file(argv[1], &total_prod, &p_head);
    if (ret != 0)
        return -1;

    // Start the traders.
    tr_head = NULL;
    int total_traders = 0;

    ret = 0;
    ret = start_traders(&argc, argv, &tr_head, &p_head, &total_traders);

    if (ret != 0) 
        return 1;

    // Send MARKET OPEN;
    send_market_open(&tr_head);

    // event loop:
    struct message* m_head = NULL;

    struct order* buy_book = NULL;
    struct order* sell_book = NULL;

    long long int fees = 0;
    while (1) {

        // Quit if all traders are dead.
        int quit = check_is_traders_dead();
        if (quit == 1)
            break;

        // Change trader status that already died.
        change_dead_trader_status(&total_traders);

        // Get messages from traders.
        get_responds(&m_head, &tr_head);
        
        // Process messages from traders.
        process_mssgs(&m_head, &buy_book, &sell_book, 
            &tr_head, &p_head, &fees);

    }

    printf("%s Trading completed\n", LOG_PREFIX);
    printf("%s Exchange fees collected: $%lld\n", 
        LOG_PREFIX, fees);

    // Wait all children to finish,
    // remove pipe files, free traders, free products.
    cleanup(&tr_head, &p_head, &m_head,
        &buy_book, &sell_book);

    return 0;
    
}

void process_mssgs(struct message** m_head, 
    struct order** buy_book, struct order** sell_book, 
    struct trader** tr_head, struct product** p_head, 
    long long int* total_fees) {

    if (trader_died > 0)
        return;

    if (*m_head != NULL) {

        // Get the message
        struct message* mssg = *m_head;
        struct trader* trader = mssg->trader;

        char mssg_str[MAX_CMD];
        memset(mssg_str, 0, MAX_CMD);

        char mssg_cpy[MAX_CMD];
        memset(mssg_cpy, 0, MAX_CMD);
        
        strcpy(mssg_str, (*m_head)->reply);
        strcpy(mssg_cpy, (*m_head)->reply);
        mssg_str[strcspn(mssg_str, ";")] = 0;
        mssg_cpy[strcspn(mssg_cpy, ";")] = 0;

        printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, 
            trader->id, mssg_str);

        *m_head = mssg->next;
        free(mssg);

        if (trader->dead == 1)
            return;

        // Process the message
        // SELL <ORDER_ID> <PRODUCT> <QTY> <PRICE>

        // Get the command word (BUY or SELL)
        char* ptr = strtok(mssg_str, " ");
        if (ptr == NULL)
            return;
            
        int ret = 0;

        if (strcmp(ptr, "BUY") == 0) {

            ret = buy_sell_cmd("BUY", ptr, trader, 
                total_fees, sell_book, buy_book, tr_head);

        } else if (strcmp(ptr, "SELL") == 0) {
            
            ret = buy_sell_cmd("SELL", ptr, trader, 
                total_fees, buy_book, sell_book, tr_head);

        } else if (strcmp(ptr, "AMEND") == 0) {
        
            ret = amend_cmd(ptr, trader, total_fees,
                buy_book, sell_book, tr_head);

        } else if (strcmp(ptr, "CANCEL") == 0) {
            
            ret = cancel_cmd(ptr, trader, buy_book, 
                sell_book, tr_head);

        } else {
            send_invalid(trader);
            return;
        }

        // there's error.
        if (ret == -1) {
            send_invalid(trader);
            return;
        }
        
        print_books(buy_book, sell_book, p_head);
        print_traders(tr_head);
    }

}

int cancel_cmd(char* cmd, struct trader* trader, 
    struct order** buy_book, struct order** sell_book,
    struct trader** tr_head) {

    cmd = strtok(NULL, " ");

    // Not enough arguments in cmd
    if (cmd == NULL)
        return -1;

    int order_id = str_2_int(cmd);

    // order_id negative or there is problem
    // when converting from str to int
    if (check_qty_price(order_id, 1) == -1)
        return -1;

    // too much args
    cmd = strtok(NULL, " ");
    if (cmd != NULL)
        return -1;

    // search the order in the books
    // based on the trader and order_id
    char* order_type = NULL;
    int o_type_int = 0;

    struct order** search_book = NULL;
    struct order** order_book = NULL;

    struct order* order = find_order(&order_type, 
        &o_type_int, &search_book, &order_book, trader, 
        &order_id, buy_book, sell_book);;

    // order does not exists
    if (order_type == NULL)
        return -1;

    // amend the qty and the price of the order
    order->qty = 0;
    order->price = 0;

    // send CANCELLED <ORDER_ID>;
    send_reply(order, "CANCELLED");

    // send MARKET <ORDER TYPE> <PRODUCT> <QTY> <PRICE>;
    send_market(order, order_type, tr_head);

    // free the order
    free_order(order, order_book, 1);

    return 0;
}

int amend_cmd(char* cmd, struct trader* trader, 
    long long int* total_fees, struct order** buy_book,
    struct order** sell_book, struct trader** tr_head) {

    // new order container
    struct order* new_order = NULL;
    
    // Get mssg info and return a new order
    int ret = get_buy_sell_info(cmd, trader, &new_order, 1);

    if (ret == -1)
        return -1;
    
    // search the order in the books
    // based on the trader and order_id
    char* order_type = NULL;
    int o_type_int = 0;

    struct order** search_book = NULL;
    struct order** order_book = NULL;

    struct order* order = find_order(&order_type, 
        &o_type_int, &search_book, &order_book, trader, 
        &(new_order->order_id), buy_book, sell_book);

    // order does not exists
    if (order_type == NULL) {
        free(new_order);
        return -1;
    }

    // amend the qty and the price of the order
    order->qty = new_order->qty;
    order->price = new_order->price;

    // send AMENDED <ORDER_ID>;
    send_reply(order, "AMENDED");

    // send MARKET <ORDER TYPE> <PRODUCT> <QTY> <PRICE>;
    send_market(order, order_type, tr_head);

    // free the temp new_order since we have the order
    free(new_order);

    // separate the order from the book
    free_order(order, order_book, 0);

    order->prev = NULL;
    order->next = NULL;

    // Search sell book/buy book for matching orders
    match_buy_sell_order(total_fees, 
        order, search_book, o_type_int);

    // Put the order to the order book if
    // there're some qty remained.
    if (order->qty > 0)
        put_order_to_book(order, order_book);
    else
        free(order);

    return 0;

}

struct order* find_order(char** order_type, int* o_type_int,
    struct order*** search_book, struct order*** order_book,
    struct trader* trader, int* order_id,
    struct order** buy_book,
    struct order** sell_book) {

    // search the order in buy book
    struct order* o_ptr = *buy_book;
    while (o_ptr != NULL) {

        if (o_ptr->trader == trader) {
            if (o_ptr->order_id == *order_id) {
                *order_type = "BUY";
                *o_type_int = 1;

                *search_book = sell_book;
                *order_book = buy_book;
                break;
            }
        }
        o_ptr = o_ptr->next;
    }

    // search the order in the sell book
    if (*o_type_int == 0) {

        o_ptr = *sell_book;
        while (o_ptr != NULL) {

            if (o_ptr->trader == trader) {
                if (o_ptr->order_id == *order_id) {
                    *order_type = "SELL";

                    *search_book = buy_book;
                    *order_book = sell_book;
                    break;
                }
            }
            o_ptr = o_ptr->next;
        }
    }

    return o_ptr;

}

int buy_sell_cmd(char* order_type, char* cmd, 
    struct trader* trader, long long int* total_fees, 
    struct order** search_book, struct order** order_book,
    struct trader** tr_head) {

    int order_type_int = 0;
    if (strcmp(order_type, "BUY") == 0)
        order_type_int = 1;

    // new order container
    struct order* new_order = NULL;
    
    // Get mssg info and return a new order
    int ret = get_buy_sell_info(cmd, trader, &new_order, 0);

    if (ret == -1)
        return -1;

    // send ACCEPTED <ORDER_ID>;
    send_reply(new_order, "ACCEPTED");

    // send MARKET <ORDER TYPE> <PRODUCT> <QTY> <PRICE>;
    send_market(new_order, order_type, tr_head);

    // Search sell book/buy book for matching orders
    match_buy_sell_order(total_fees, 
        new_order, search_book, order_type_int);

    // Put the order to the order book if
    // there're some qty remained.
    if (new_order->qty > 0)
        put_order_to_book(new_order, order_book);
    else
        free(new_order);

    return 0;

}

void match_buy_sell_order(long long int* total_fees,
    struct order* new_order, struct order** book, 
    int buy) {

    int max = 0;
    int min = 1000000;
    struct order* o_ptr = NULL;

    while(new_order->qty > 0) {
        // search for the highest possible price.
        max = 0;
        min = 1000000;
        o_ptr = *book;

        while (o_ptr != NULL) {

            if (strcmp(o_ptr->prod_name, 
                new_order->prod_name) == 0) {
                
                if (buy == 1) {
                    // matching a new buy order
                    if (o_ptr->price < min && 
                        o_ptr->price <= new_order->price) {
                        
                        min = o_ptr->price;
                    }
                } else {
                    // matching a new sell order
                    if (o_ptr->price > max && 
                        o_ptr->price >= new_order->price) {
                        
                        max = o_ptr->price;
                    }
                    
                }
                
            }

            o_ptr = o_ptr->next;
        }

        // there's no match available.
        if (buy == 1) {
            if (min == 1000000)
                break;
        } else {
            if (max == 0)
                break;
        }

        // match the order that has the highest price
        // with the new order.
        int match = 0;
        if (buy == 1) {
            match = min;
        }   
        else {
            match = max;
        }
            
        o_ptr = *book;

        while (o_ptr != NULL) {

            if (strcmp(o_ptr->prod_name, 
                new_order->prod_name) == 0) {

                if (o_ptr->price == match) {

                    int qty = 0;
                    if (o_ptr->qty >= new_order->qty) {
                        o_ptr->qty -= new_order->qty;
                        qty = new_order->qty;
                        new_order->qty = 0;

                    } else {
                        new_order->qty -= o_ptr->qty;
                        qty = o_ptr->qty;
                        o_ptr->qty = 0;
                    }

                    // calculate the value and fee
                    int price = o_ptr->price;
                    long long int value = (long long int) 
                        price*qty;
                    long long int fee = (long long int) 
                        round(0.01* (long double) value);

                    // add fee to total fees
                    *total_fees+=fee;

                    // add the money
                    struct trader* seller = NULL;
                    struct trader* buyer = NULL;

                    if (buy == 1) {
                        seller = o_ptr->trader;
                        buyer = new_order->trader;
                        put_money_n_product(buyer, -fee, 0, 
                            new_order->prod_name);
                    } else {
                        seller = new_order->trader;
                        buyer = o_ptr->trader;
                        put_money_n_product(seller, -fee, 0, 
                            new_order->prod_name);
                    }

                    put_money_n_product(seller, value, -qty, 
                        new_order->prod_name);
                    put_money_n_product(buyer, -value, qty,
                        new_order->prod_name);

                    // print
                    print_match(o_ptr, new_order, &value, &fee);

                    // send FILL <ORDER_ID> <QTY>;
                    send_fill(new_order, &qty);
                    send_fill(o_ptr, &qty);
                }
            }

            // free the o_ptr if it's empty
            struct order* current = o_ptr;
            o_ptr = o_ptr->next;

            if (current->qty == 0) {
                free_order(current, book, 1);
            }
            
        }
    }
    
}

void put_money_n_product(struct trader* trader, 
    long long int value, int qty, char* prod_name) {
    
    struct product* p_ptr = trader->products;
    while(p_ptr != NULL) {

        if (strcmp(p_ptr->name, prod_name) == 0)
            break;
        p_ptr = p_ptr->next;
    }

    p_ptr->num_of_prod += qty;
    p_ptr->money += value;

}

void put_order_to_book(struct order* new_order, 
    struct order** book) {
    
    // Put new order at the head of the book
    // if the head of the book is NULL.
    if (*book == NULL) {
        *book = new_order;
        return;
    }
    
    // put the new order at the end
    // of the linked list.
    struct order* o_ptr = *book;
    while (o_ptr->next != NULL)
        o_ptr = o_ptr->next;

    o_ptr->next = new_order;
    new_order->prev = o_ptr;
}

int get_buy_sell_info(char* ptr, struct trader* trader, 
    struct order** new_order, int amend) {

    ptr = strtok(NULL, " ");

    // Not enough arguments in cmd
    if (ptr == NULL)
        return -1;

    int order_id = str_2_int(ptr);

    // order_id negative or there is problem
    // when converting from str to int
    if (check_qty_price(order_id, 1) == -1)
        return -1;

    if (amend == 0) {
        // order id is not increment by 1
        if (order_id != trader->order_id)
            return -1;
    }

    char prod_name[MAX_PROD_NAME];

    if (amend == 0) {
        // get product name
        ptr = strtok(NULL, " ");

        // Not enough arguments in cmd
        if (ptr == NULL)
            return -1;

        // product name longer than max
        if (strlen(ptr) > MAX_PROD_NAME-1)
            return -1;

        int exists = 0;
        struct product* p_ptr = trader->products;

        while (p_ptr != NULL) {
            if (strcmp(p_ptr->name, ptr) == 0) {
                exists = 1;
                break;
            }
            p_ptr = p_ptr->next;
        }

        // product name doesnt exists
        if (exists == 0) 
            return -1;

        memset(prod_name, 0, MAX_PROD_NAME);
        strcpy(prod_name, ptr);
    }

    // get qty
    ptr = strtok(NULL, " ");

    // Not enough arguments in cmd
    if (ptr == NULL)
        return -1;

    int qty = str_2_int(ptr);

    if (check_qty_price(qty, 0) == -1)
        return -1;

    // get price 
    ptr = strtok(NULL, " ");

    // Not enough arguments in cmd
    if (ptr == NULL)
        return -1;

    int price = str_2_int(ptr);

    if (check_qty_price(price, 0) == -1)
        return -1;

    // too much args
    ptr = strtok(NULL, " "); 
    if (ptr != NULL)
        return -1;

    // create new order
    struct order* new = NULL;
    new = create_new_order(&order_id, 
        prod_name, &qty, &price, trader);

    if (new == NULL)
        return -1;

    *new_order = new;

    if (amend == 0)
        trader->order_id+=1;

    return 0;

}

int check_qty_price(int num, int order_id) {

    if (order_id == 0) {
        // price and qty cannot zero
        if (num == 0)
            return -1;
    }

    // num is negative
    // or there is problem
    // when converting from str to int
    if (num < 0) 
        return -1;

    // num is more than max
    if (num > MAX_NUM)
        return -1;
    

    return 0;
}

struct order* create_new_order(int* order_id, 
    char* prod_name, int* qty, int* price, 
    struct trader* trader) {
    
    struct order* new = (struct order*) malloc(sizeof(
        struct order));
    
    new->trader = trader;
    new->order_id = *order_id;

    strcpy(new->prod_name, prod_name);
    new->price = *price;
    new->qty = *qty;

    new->next = NULL;
    new->prev = NULL;

    return new;

}

void free_order(struct order* target, struct order** book,
    int free_fr) {

    struct order* prev = target->prev;
    struct order* next = target->next;

    if (*book == target) {
        *book = next;
    }

    if (prev != NULL) {
        prev->next = next;
    }

    if (next != NULL) {
        next->prev = prev;
    }

    if (free_fr == 1)
        free(target);
}

void print_match(struct order* o_ptr, 
    struct order* new_order, long long int* value, 
    long long int* fee) {

    int old_trader_id = o_ptr->trader->id;
    int new_trader_id = new_order->trader->id;

    printf("%s Match: Order %d [T%d], New Order %d [T%d], ", 
        LOG_PREFIX, o_ptr->order_id, old_trader_id, 
        new_order->order_id, new_trader_id);
    printf("value: $%lld, fee: $%lld.\n", *value, *fee);

}

void print_traders(struct trader** tr_head) {

    printf("%s\t--POSITIONS--\n", LOG_PREFIX);

    struct trader* t_ptr = *tr_head;
    while (t_ptr != NULL) {

        printf("%s\tTrader %d:", LOG_PREFIX, t_ptr->id);

        struct product* p_ptr = t_ptr->products;
        while (p_ptr != NULL) {

            printf(" %s %lld ($%lld)", p_ptr->name, 
                p_ptr->num_of_prod, p_ptr->money);

            p_ptr = p_ptr->next;

            if (p_ptr != NULL)
                printf(",");
            else
                printf("\n");
        }
        t_ptr = t_ptr->next;
    }
}

void print_books(struct order** buy_book, 
    struct order** sell_book, struct product** p_head) {
    
    printf("[PEX]\t--ORDERBOOK--\n");

    struct product* p_ptr = *p_head;
    while (p_ptr != NULL) {

        // get the levels from sell and buy books
        int sell_levels = 0;
        get_levels(sell_book, &sell_levels, p_ptr->name);

        int buy_levels = 0;
        get_levels(buy_book, &buy_levels, p_ptr->name);

        printf("%s\tProduct: %s; Buy levels: %d; "
            "Sell levels: %d\n", LOG_PREFIX, p_ptr->name, 
            buy_levels, sell_levels);

        // print the levels from sell and buy books
        print_levels(sell_book, &sell_levels, "SELL", 
            p_ptr->name);

        print_levels(buy_book, &buy_levels, "BUY", 
            p_ptr->name);

        p_ptr = p_ptr->next;

    }

}

void print_levels(struct order** book, int* levels, 
    char* order_type, char* prod_name) {

    int old_highest = 1000000;
    int new_highest = 0;
    for (int i = 0; i < *levels; i++) {
        
        get_highest_below(book, 
            &old_highest, &new_highest, prod_name);

        print_level(&new_highest, book, 
            prod_name, order_type);

        old_highest = new_highest;
        new_highest = 0;

    }

}

void print_level(int* new_highest, 
    struct order** book, char* prod_name, 
    char* order_type) {

    long long int total_qty = 0;
    int total_order = 0;

    struct order* o_ptr = *book;
    while (o_ptr != NULL) {
        
        if (strcmp(o_ptr->prod_name, prod_name) == 0) {
            //printf("in print level: %s %d %d\n", o_ptr->prod_name, o_ptr->qty, o_ptr->price);
            if (o_ptr->price == *new_highest) {
                total_qty+=o_ptr->qty;
                total_order+=1;
            }
        }
        o_ptr = o_ptr->next;   
    }

    if (total_order > 1) {
        printf("%s\t\t%s %lld @ $%d (%d orders)\n", LOG_PREFIX, 
            order_type, total_qty, *new_highest, total_order);
    } else {
        printf("%s\t\t%s %lld @ $%d (%d order)\n", LOG_PREFIX, 
            order_type, total_qty, *new_highest, total_order);
    }
}

void get_highest_below(struct order** book, 
    int* old_highest, int* new_highest, 
    char* prod_name) {

    struct order* o_ptr = *book;
    while (o_ptr != NULL) {
        
        if (strcmp(o_ptr->prod_name, prod_name) == 0) {
            if (o_ptr->price > *new_highest &&
                o_ptr->price < *old_highest) {
                
                *new_highest = o_ptr->price;
            }
        }

        o_ptr = o_ptr->next;
    }

}

void get_levels(struct order** book, int* levels, 
    char* prod_name) {

    *levels = 0;

    int old_highest = 1000000;
    int new_highest = 0;
    while (1) {
        
        get_highest_below(book, 
            &old_highest, &new_highest, prod_name);

        if (new_highest == 0)
            break;

        *levels+=1;

        old_highest = new_highest;
        new_highest = 0;

    }

}

void send_invalid(struct trader* trader) {

    // dont send to a dead trader
    if (trader->dead == 1)
        return;

    char* message = "INVALID;";

    ssize_t ret = write(trader->ex_fifo_fd, message, strlen(message));
    
    if (ret == -1 && errno == EPIPE)
        declare_dead(trader);
    else
        kill(trader->pid, SIGUSR1);
}

void send_fill(struct order* order, int* qty) {

    struct trader* trader = order->trader;

    // dont send to a dead trader
    if (trader->dead == 1)
        return;

    char message[MAX_REPLY_LEN];
    memset(message, 0, MAX_REPLY_LEN);

    sprintf(message, "FILL %d %d;", order->order_id, *qty);
    ssize_t ret = write(trader->ex_fifo_fd, message, 
        strlen(message));

    if (ret == -1 && errno == EPIPE)
        declare_dead(trader);
    else
        kill(order->trader->pid, SIGUSR1);
}

// for MARKET <ORDER TYPE> <PRODUCT> <QTY> <PRICE>;
void send_market(struct order* order, char* order_type,
    struct trader** tr_head) {

    char message[MAX_REPLY_LEN];
    memset(message, 0, MAX_REPLY_LEN);

    sprintf(message, "MARKET %s %s %d %d;", order_type, 
        order->prod_name, order->qty, order->price);

    struct trader* t_ptr = *tr_head;
    while(t_ptr != NULL) {

        // dont send to the person who make the order
        if (t_ptr == order->trader) {
            t_ptr = t_ptr->next;
            continue;
        }

        // dont send to a dead trader
        if (t_ptr->dead == 1) {
            t_ptr = t_ptr->next;
            continue;
        }

        ssize_t ret = write(t_ptr->ex_fifo_fd, message, 
            strlen(message));
        
        if (ret == -1 && errno == EPIPE)
            declare_dead(t_ptr);
        else
            kill(t_ptr->pid, SIGUSR1);

        t_ptr = t_ptr->next;
    }
}

// For ACCEPTED <ORDER_ID>; AMENDED <ORDER_ID>; 
// CANCELLED <ORDER_ID>;
void send_reply(struct order* order, char* reply) {

    struct trader* trader = order->trader;

    // dont send to a dead trader
    if (trader->dead == 1)
        return;

    char message[MAX_REPLY_LEN];
    memset(message, 0, MAX_REPLY_LEN);

    sprintf(message, "%s %d;", reply, order->order_id);
    ssize_t ret = write(trader->ex_fifo_fd, message, 
        strlen(message));

    if (ret == -1 && errno == EPIPE)
        declare_dead(trader);
    else
        kill(trader->pid, SIGUSR1);

}

void get_responds(struct message** m_head, 
    struct trader** tr_head) {

    if (responds == 0)
        return;

    if (trader_died > 0)
        return;

    int pid = *(responds_queue+r_idx);
    r_idx++;

    if (r_idx == max_queue)
        r_idx = 0;

    //match pid.
    struct trader* t_ptr = *tr_head;
    while(t_ptr != NULL) {
        
        if (t_ptr->pid == pid) {

            if (t_ptr->dead == 1)
                return;

            get_respond(m_head, t_ptr);
            responds--;
        }

        t_ptr = t_ptr->next;
    }
}

void get_respond(struct message** m_head, struct trader* trader) {

    // Get the message by iterating one char at a time
    // until it reaches ";". then, stop.
    char reply[MAX_CMD];
    memset(reply, 0x0, MAX_CMD);

    for(int i = 0; i < MAX_CMD-1; i++) {
        
        read(trader->tr_fifo_fd, &reply[i], 1);

        if (reply[i] == ';')
            break;

    }

    // put the received message to the linked list of messsage.
    struct message* new = (struct message*) malloc(sizeof(struct message));

    strcpy(new->reply, reply);
    new->trader = trader;
    new->next = NULL;

    if (*m_head == NULL) {
        new->prev = NULL;
        *m_head = new;
    } else {

        struct message* m_ptr = *m_head;
        while (m_ptr->next != NULL)
            m_ptr = m_ptr->next;
        
        m_ptr->next = new;
        new->prev = m_ptr;
    }

}

int check_is_traders_dead() {

    int all_dead = 1;

    struct trader* t_ptr = tr_head;
    while(t_ptr != NULL) {
        
        if (t_ptr->dead == 0) {
            all_dead = 0;
            break;
        }

        t_ptr = t_ptr->next;
    }

    return all_dead;
}

void change_dead_trader_status(int* total_traders) {

    for (int i = 0; i < *total_traders; i++) {

        int pid = *(dead_traders+i);
        if (pid != 0) {
            
            struct trader* t_ptr = tr_head;
            while(t_ptr != NULL) {

                if (t_ptr->pid == pid) {
                    declare_dead(t_ptr);
                    trader_died--;
                    break;
                }
                t_ptr = t_ptr->next;
            }

            *(dead_traders+i) = 0;
            
        }
    }

}

void declare_dead(struct trader* trader) {
    trader->dead = 1;

    if (trader->print_dead == 0) {
        printf("%s Trader %d disconnected\n", 
            LOG_PREFIX, trader->id);
        trader->print_dead = 1;
    }
}

void cleanup(struct trader** tr_head, struct product** p_head, 
    struct message** m_head, struct order** buy_book, 
    struct order** sell_book) {

    // Waiting for all children to finish.
	// Reference from: https://stackoverflow.com/questions/19461744/
	// how-to-make-parent-wait-for-all-child-processes-to-finish
	int wpid = 0;
	int status = 0;
	while ((wpid = wait(&status)) > 0);
    //printf("%s Waiting for children completed\n", LOG_PREFIX);

    // remove the pipe files
    struct trader* t_ptr = *tr_head;
    while(t_ptr != NULL) {
        unlink(t_ptr->ex_fifo_name);
        unlink(t_ptr->tr_fifo_name);
        t_ptr = t_ptr->next;
    }
    //printf("%s Removing named-pipe's files completed\n", LOG_PREFIX);

    // free the trader linked list
    t_ptr = *tr_head;
    while (t_ptr != NULL) {
        struct trader* current = t_ptr;
        t_ptr = t_ptr->next;

        // free trader's products
        struct product* p_ptr = current->products;
        while (p_ptr != NULL) {
            struct product* cur_prod = p_ptr;
            p_ptr = p_ptr->next;
            free(cur_prod);
        }

        // free trader
        free(current);
    }
    //printf("%s Freeing trader linked list completed\n", LOG_PREFIX);

    // free products
    free_products(p_head);

    // free dead_traders
    free(dead_traders);

    // free responds queue
    free(responds_queue);

    // free messages
    struct message* m_ptr = *m_head;
    while (m_ptr != NULL) {
        struct message* current = m_ptr;
        m_ptr = m_ptr->next;

        free(current);
    }

    // free buy book
    struct order* o_ptr = *buy_book;
    while(o_ptr != NULL) {
        struct order* current = o_ptr;
        o_ptr = o_ptr->next;
        
        free(current);
    }

    // free sell book
    o_ptr = *sell_book;
    while(o_ptr != NULL) {
        struct order* current = o_ptr;
        o_ptr = o_ptr->next;
        
        free(current);
    }

}

void free_products(struct product** p_head) {
    // free products
    struct product* p_ptr = *p_head;
    while (p_ptr != NULL) {
        struct product* current = p_ptr;
        p_ptr = p_ptr->next;

        free(current);
    }
}

void send_market_open(struct trader** tr_head) {

    char* message = "MARKET OPEN;";
    
    struct trader* t_ptr = *tr_head;
    while (t_ptr != NULL) {

        if (t_ptr->dead == 1) {
            t_ptr = t_ptr->next;
            continue;
        }

        ssize_t ret = write(t_ptr->ex_fifo_fd, message, 
            strlen(message));
        
        if (ret == 1 && errno == EPIPE)
            declare_dead(t_ptr);
        else
            kill(t_ptr->pid, SIGUSR1);

        t_ptr = t_ptr->next;
    }

}

int start_traders(int* argc, char** argv,
    struct trader** tr_head, struct product** p_head, 
    int* total_traders) {

    int id = 0;
    int ex_fd = -1;
    int tr_fd = -1;

    for (int i = 2; i < *argc; i++) {

        // FIFO names container.
        char ex_fifo[EX_FIFO_NAME_LEN];
        char tr_fifo[TR_FIFO_NAME_LEN];

        // Creating the pe_exchange_id fifo.
		sprintf(ex_fifo, FIFO_EXCHANGE, id);
		ex_fifo[strlen(ex_fifo)] = '\0';

		int ret = mkfifo(ex_fifo, 0666);
		if (ret == -1) {
			printf("parent: making fifo: %s, fifo: %s\n", 
				strerror(errno), ex_fifo);
		}

		printf("%s Created FIFO %s\n", LOG_PREFIX, ex_fifo);

		// Creating the pe_trader_id fifo.
		sprintf(tr_fifo, FIFO_TRADER, id);
		tr_fifo[strlen(tr_fifo)] = '\0';

		ret = mkfifo(tr_fifo, 0666);
		if (ret == -1) {
			printf("parent: making fifo: %s, fifo: %s\n", 
				strerror(errno), tr_fifo);
		}

		printf("%s Created FIFO %s\n", LOG_PREFIX, tr_fifo);

        // create child trader

        printf("%s Starting trader %d (%s)\n", LOG_PREFIX, 
            id, argv[i]);

        // Converting id to str.
		char str_id[MAX_DIGIT];
		sprintf(str_id, "%d", id);

        int pid = fork();
		if (pid == -1) {
			printf("Failed to make the child.");
		} else if (pid == 0) {
			int ret = execl(argv[i], argv[i], str_id, (char*)0);

			if (ret == -1) {
				printf("Program %s was not found.\n", argv[i]);
				return 1;
			} else if (ret >= 0) {
				printf("Execl function failed.\n");
				return 1;
			}
		}

        // connect to named pipes

        // Connecting to the pe_exchange_id fifo in write only.
		ex_fd = open(ex_fifo, O_WRONLY);
		if (ex_fd == -1) {
			printf("parent: writing fifo: %s, errno: %d\n", 
				strerror(errno), errno);
            return 1;
		}

		printf("%s Connected to %s\n", LOG_PREFIX, ex_fifo);
		
		// Connecting to the pe_trader_id fifo in read only.
		tr_fd = open(tr_fifo, O_RDONLY|O_NONBLOCK);
		if (tr_fd == -1) {
			printf("parent: writing fifo: %s, errno: %d\n", 
				strerror(errno), errno);
            return 1;
		}

		printf("%s Connected to %s\n", LOG_PREFIX, tr_fifo);

        create_trader(&id, &pid, &ex_fd, &tr_fd, ex_fifo, tr_fifo, 
            tr_head, p_head);

        id++;

    }

    *total_traders = id+1;
    dead_traders = (int*) malloc(
        sizeof(int)*(*total_traders));

    max_queue = (*total_traders)*50;
    responds_queue = (int*) malloc(
        sizeof(int)*max_queue);

    return 0;
}

void create_trader(int* id, int* pid, int* ex_fd, int* tr_fd,
    char* ex_fifo, char* tr_fifo, struct trader** tr_head, 
    struct product** p_head) {

    struct trader* new = (struct trader*) 
        malloc(sizeof(struct trader));

    new->id = *id;
    new->pid = *pid;
    new->order_id = 0;
    new->dead = 0;
    new->print_dead = 0;

    new->ex_fifo_fd = *ex_fd;
    new->tr_fifo_fd = *tr_fd;

    strcpy(new->ex_fifo_name, ex_fifo);
    strcpy(new->tr_fifo_name, tr_fifo);

    copy_products(new, p_head);

    if (*tr_head == NULL) {
        *tr_head = new;
        new->prev = NULL;
    } else {
        struct trader* t_ptr = *tr_head;

        while (t_ptr->next != NULL)
            t_ptr = t_ptr->next;

        t_ptr->next = new;
        new->prev = t_ptr;
    }

    new->next = NULL;
}

void copy_products(struct trader* trader, 
    struct product** p_head) {

    struct product* new_products = NULL;

    struct product* old_ptr = *p_head;

    while (old_ptr != NULL) {
        
        struct product* new = (struct product*) malloc(
            sizeof(struct product));

        new->num_of_prod = 0;
        new->money = 0;
        strcpy(new->name, old_ptr->name);

        new->next = NULL;

        if (new_products == NULL) {
            new_products = new;
        }
        else {
    
            struct product* new_ptr = new_products;
            while(new_ptr->next != NULL)
                new_ptr = new_ptr->next;

            new_ptr->next = new;
        }

        old_ptr = old_ptr->next;
    }

    trader->products = new_products;
}

int read_prod_file(char* file_name, int* total_prod, 
    struct product** p_head) {

    FILE *f_ptr = fopen(file_name, "r");
    char buff[MAX_PROD_NAME+1];
    memset(buff, 0, MAX_PROD_NAME+1);

    // this is for when iterating per char.
    char buff2[ONE_CHAR];
    memset(buff2, 0, ONE_CHAR);

    if (fgets(buff, MAX_PROD_NAME+1, f_ptr) != NULL) {

        // remove newline.
        buff[strcspn(buff, "\n")] = 0;

        // read the product number.
        *total_prod = str_2_int(buff);
        
        if (*total_prod == -1) {
            
            fclose(f_ptr);
            return -1;
        }

    } else {
        // there's no product number.
        fclose(f_ptr);
        return -1;
    }
    
    int total = 0;
    while (total < *total_prod) {

        if (fgets(buff, MAX_PROD_NAME+1, f_ptr) == NULL)
            break;

        // check if the line exceed max product name length.
        if (is_there_newline(buff) == 0) {

            if (strlen(buff) >= MAX_PROD_NAME) {
                int quit = 0;
                int cont = 0;
                // just read one char at a time until reach
                // newline and move to the next line.
                while(1) {
                    memset(buff2, 0, ONE_CHAR);
                    if (fgets(buff2, ONE_CHAR, f_ptr) 
                        == NULL) {
                        quit = 1;
                        break;
                    }
                        
                    if (*buff2 == '\n') {
                        cont = 1;
                        break;
                    }
                }

                if (quit == 1)
                    break;

                if (cont == 1)
                    continue;
            }
        }
        
        // remove newline.
        buff[strcspn(buff, "\n")] = 0;

        // create a new product container.
        struct product* new = (struct product*) 
            malloc(sizeof(struct product));

        if (new == NULL) {
            free_products(p_head);
            fclose(f_ptr);
            return -1;
        }

        strcpy(new->name, buff);
        new->next = NULL;

        // put the new product to the list of product.
        if (*p_head == NULL) {
            *p_head = new; 
        }
        else {
            struct product* p_ptr = *p_head;

            while(p_ptr->next != NULL)
                p_ptr = p_ptr->next;

            p_ptr->next = new;
        }

        memset(buff, 0, MAX_PROD_NAME+1);
        total++;
    }

    fclose(f_ptr);

    if (total < *total_prod) {
        free_products(p_head);
        return -1;
    }

    // Print result.
    printf("%s Trading %d products:", LOG_PREFIX, 
        *total_prod);

    struct product* p_ptr = *p_head;
    while (p_ptr != NULL) {
        printf(" %s", p_ptr->name);
        p_ptr = p_ptr->next;
    }

    printf("\n");    

    return 0;

}

int is_there_newline(char* line) {

    int len = strlen(line);
    int exists = 0;
    for (int i = 0; i < len; i++) {
        if (*(line+i) == '\n') {
            exists = 1;
            break;
        }
    }
    return exists;
}

void sigchld_handler(int sig, siginfo_t *si, 
    void *ucontext) {

    int pid = si->si_pid;

    *(dead_traders+dead_idx) = pid;

    dead_idx++;
    trader_died++;
    return;
}

void sigusr1_handler(int sig, siginfo_t *si, 
    void *ucontext) {
    int pid = si->si_pid;

    *(responds_queue+w_idx) = pid;
    w_idx++;

    if (w_idx == max_queue)
        w_idx = 0;

    responds++;
	return;

}

void print_book(struct order** buy_book, 
    struct order** sell_book) {

    printf("sell book:\n");
    struct order* o_ptr = *sell_book;
    while (o_ptr != NULL) {

        printf("SELL %d %s %d %d\n", o_ptr->order_id, 
            o_ptr->prod_name, o_ptr->qty, o_ptr->price);
        o_ptr = o_ptr->next;
    }
    printf("buy book:\n");
    o_ptr = *buy_book;
    for (int i = 0; i < 10; i++) {

        if (o_ptr == NULL)
            break;

        printf("BUY %d %s %d %d\n", o_ptr->order_id, 
            o_ptr->prod_name, o_ptr->qty, o_ptr->price);
        o_ptr = o_ptr->next;
    }

}

int str_2_int(char* line) {
	// Converting from string to integer and error 
	// checking took reference from: 
	// https://stackoverflow.com/questions/7021725/
	// how-to-convert-a-string-to-integer-in-c
	char* end;
	int result = (int) strtol(line, &end, 10);

	if (result > INT_MAX 
		|| (errno == ERANGE && result == LONG_MAX)) {
        //printf("overflow\n");
		return -1;
	}
	if (result < INT_MIN 
		|| (errno == ERANGE && result == LONG_MIN)) {
        //printf("underflow\n");
		return -1;
	}
	if (*end != '\0') {
        //printf("not int\n");
		return -1;
	}
	return result;
}
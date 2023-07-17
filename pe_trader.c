#include "pe_trader.h"

unsigned int responds = 0;
int parent = 0;

char* id_str = NULL;

int main(int argc, char ** argv) {
    
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    //printf("[T%s] Starting\n", argv[1]);
    id_str = argv[1];

    // register signal handler

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = sigusr1_handler;
    sa.sa_flags = SA_SIGINFO; /* Important. */

	sigaction(SIGUSR1, &sa, NULL);

    // connect to named pipes
    int ex_fd = -1;
    int tr_fd = -1;
    connect_2_pipes(&ex_fd, &tr_fd, argv[1]);

    struct message* m_head = NULL;
    
    // event loop:
    int quit = 0;
    int order_id = 0;
    int pending = -1;

    while (1) {
        
        // Get the responds from the pe_exchange.
        get_responds(&m_head, &ex_fd);

        // Process the responds from the pe_exchange.
        process_mssgs(&m_head, &quit, &order_id, &tr_fd, &pending);

        if (quit == 1)
            break;
    }

    // Free message queue.
    free_queue(&m_head);

    // wait for exchange update (MARKET message)
    // send order
    // wait for exchange confirmation (ACCEPTED message)

    return 0;
    
}

void sigusr1_handler(int sig, siginfo_t *si, void *ucontext)
{
    parent = si->si_pid;
    responds++;
	return;

}

void print_mssgs(struct message** m_head) {

    struct message* m_ptr = *m_head;

    while (m_ptr != NULL) {
        printf("MESSAGE: %s\n", m_ptr->reply);
        m_ptr = m_ptr->next;
    }
}

void process_mssgs(struct message** m_head, int* quit, 
    int* order_id, int* tr_fd, int* pending) {

    struct message* m_ptr = *m_head;
    int br = 0;

    while(m_ptr != NULL) {

        //print_mssgs(m_head);

        char reply_cpy[MAX_REPLY_LEN];
        strcpy(reply_cpy, m_ptr->reply);

        //printf("[T%s] PROCESSING: %s\n", id_str, reply_cpy);

        char* ptr = NULL;
        ptr = strtok(reply_cpy, DELIM);

        if(strcmp("MARKET", ptr) == 0) {

            ptr = strtok(NULL, DELIM);
            
            if (strcmp("SELL", ptr) == 0) {

                char product[MAX_PROD_NAME];
                int qty = 0;
                int price = 0;

                // Get the product.
                ptr = strtok(NULL, DELIM);
                strcpy(product, ptr);

                // Get the quantity.
                ptr = strtok(NULL, DELIM);
                qty = str_2_int(ptr);

                // Exit if the qty is >= 1000.
                if (qty >= 1000) {
                    *quit = 1;
                    break;
                }

                if (*pending != -1) {
                    //printf("CONTINUE\n");
                    m_ptr = m_ptr->next;
                    continue;
                }

                // Get the price.
                ptr = strtok(NULL, DELIM);
                ptr[strcspn(ptr, ";")] = 0; // removing ;

                price = str_2_int(ptr);

                // Buy the product if quantity not 0.
                if (qty > 0) {

                    // Send the BUY order to the exchange.
                    
                    char message[MAX_CMD];
                    sprintf(message, "BUY %d %s %d %d;", *order_id,
                        product, qty, price);
                    
                    write(*tr_fd, message, strlen(message));
                    kill(parent, SIGUSR1);

                    *pending = *order_id;
                    *order_id = *order_id +1;
                    
                    //printf("[T%s] SENT: %s\n", id_str, message);
                }
            }

        } else if (strcmp("ACCEPTED", ptr) == 0) {
            
            ptr = strtok(NULL, DELIM);
            ptr[strcspn(ptr, ";")] = 0;

            int id = str_2_int(ptr);

            if (id == *pending) {
                *pending = -1;
                br = 1;
                //printf("ID: %d, pending: %d\n", id, *pending);
            }
                
        } 

        struct message* target = m_ptr;

        m_ptr = m_ptr->next;

        struct message* next = target->next;
        struct message* prev = target->prev;

        if (target == *m_head)
            *m_head = next;

        if (next != NULL) {
            next->prev = prev;
        }

        if (prev != NULL) {
            prev->next = next;
        }

        free(target);

        if (br == 1)
            break;
    }
}

void free_queue(struct message** m_head) {
    struct message* m_ptr = *m_head;
    while (m_ptr != NULL) {
        struct message* current = m_ptr;
        m_ptr = m_ptr->next;

        free(current);
    }
}

void get_responds(struct message** m_head, int* ex_fd) {

    while (responds > 0) {

            char reply[MAX_REPLY_LEN];
            memset(reply, 0x0, MAX_REPLY_LEN);

            for(int i = 0; i < MAX_REPLY_LEN-1; i++) {
                
                read(*ex_fd, &reply[i], 1);

                if (reply[i] == ';')
                    break;

            }

            struct message* new = (struct message*) malloc(sizeof(struct message));

            strcpy(new->reply, reply);
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
            //printf("[T%s] RECEIVED: %s\n", id_str, new->reply);

            responds--;
        }
}

void connect_2_pipes(int* ex_fd, int* tr_fd, char* id_str) {
    
    // FIFO names container.
    char ex_fifo[EX_FIFO_NAME_LEN];
    char tr_fifo[TR_FIFO_NAME_LEN];

    int id = str_2_int(id_str);
    sprintf(ex_fifo, FIFO_EXCHANGE, id);
    sprintf(tr_fifo, FIFO_TRADER, id);

    // Connecting to the pe_exchange_id fifo in read only.
    *ex_fd = -1;
    *ex_fd = open(ex_fifo, O_RDONLY);
    if (*ex_fd == -1) {
        printf("parent: writing fifo: %s, errno: %d\n", 
            strerror(errno), errno);
    }

    //printf("[T%s] Connected to %s\n", id_str, ex_fifo);
    
    // Connecting to the pe_trader_id fifo in write only.
    *tr_fd = -1;
    *tr_fd = open(tr_fifo, O_WRONLY);
    if (*tr_fd == -1) {
        printf("parent: writing fifo: %s, errno: %d\n", 
            strerror(errno), errno);
    }

    //printf("[T%s] Connected to %s\n", id_str, tr_fifo);
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
		printf("Num is overflow.\n");
		return -1;
	}
	if (result < INT_MIN 
		|| (errno == ERANGE && result == LONG_MIN)) {
		printf("Num is underflow.\n");
		return -1;
	}
	if (*end != '\0') {
		printf("Num string is inconvertible.\n");
		return -1;
	}
	return result;
}


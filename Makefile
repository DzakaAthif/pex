CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -fsanitize=address,leak
LDFLAGS=-lm
CCOV=-fprofile-arcs -ftest-coverage
BINARIES=pe_exchange pe_trader
TRADER1=trader1
EXCHANGE=pe_exchange

all: $(BINARIES)

.PHONY: clean tests run_tests
clean:
	rm -f $(BINARIES)
	rm -f $(EXCHANGE)
	rm -f $(TRADER) $(TRADER1)
	rm -f test.txt test
	rm -f cov.txt
	rm -f pe_exchange.c.gcov

build:
	$(CC) $(CFLAGS) $(LDFLAGS) $(TRADER1).c -o $(TRADER1)
	$(CC) $(CFLAGS) $(EXCHANGE).c -o $(EXCHANGE) $(LDFLAGS)

build2:
	$(CC) $(CFLAGS) $(LDFLAGS) $(TRADER1).c -o $(TRADER1)
	$(CC) $(CFLAGS) $(CCOV) $(EXCHANGE).c -o $(EXCHANGE) $(LDFLAGS)

run:
	./$(EXCHANGE) products.txt ./$(TRADER1)

submit:
	make clean
	git add .
	git commit -m "trying"
	git push

tests:
	bash tests.sh

run_tests:
	bash run_tests.sh


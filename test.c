#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main() {

    char* str1 = "athif\n";
    char* str2 = "dzaka";

    printf("str1: %d\n", is_there_newline(str1));
    printf("str2: %d\n", is_there_newline(str2));

    return 0;
}
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// #define NFORK 10
// #define IO 5

int main(int argc, char *argv[]){
    int new_pri, pid;

    if (argc != 2){
        fprintf(2, "Incorrect number of arguments to setpriority\n");
        exit(1);
    }
    pid = atoi(argv[2]);
    new_pri = atoi(argv[1]);

    printf("Setpriority executed: %d\n", set_priority(new_pri, pid));
    exit(0);
}
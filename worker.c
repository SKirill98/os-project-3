#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

const int BUFF_SZ = sizeof(int)*2;
int shm_key;
int shm_id;

int main(int argc, char *argv[]) {
    
    if (argc != 3) {
        fprintf(stderr, "Usage: worker sec nonosec\n");
        exit(1);
    }
    
    int run_sec = atoi(argv[1]);
    int run_nano = atoi(argv[2]);
    
    
    // Shared memory
    int shm_key = ftok("oss.c", 'R');
    if (shm_key <= 0 ) {
        fprintf(stderr,"Child:... Error in ftok\n");
        exit(1);
    }
    
    // Create shared memory segment
    int shm_id = shmget(shm_key,BUFF_SZ,0700);
    if (shm_id <= 0 ) {
        fprintf(stderr,"child:... Error in shmget\n");
        exit(1);
    }
    
    // Attach to the shared memory segment
    int *clock = (int *)shmat(shm_id,0,0);
    if (clock == (void *) -1) {
        fprintf(stderr,"Child:... Error in shmat\n");
        exit(1);
    }
    
    // Access the shared memory
    int *sec = &(clock[0]);
    int *nano = &(clock[1]);

    int start_sec = *sec;
    int start_nano = *nano;

    int term_sec = start_sec + run_sec;
    int term_nano = start_nano + run_nano;

    if (term_nano >= 1000000000) {
        term_sec++;
        term_nano -= 1000000000;
    }

    printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
    printf("SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n", start_sec, start_nano, term_sec, term_nano);
    printf("--Just Starting\n");

    int last_printed_sec = start_sec;

    while (1) {

        if (*sec > term_sec || (*sec == term_sec && *nano >= term_nano)) {
            printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
            printf("SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n", start_sec, start_nano, term_sec, term_nano);
            printf("--Terminating\n");
            break;
        }

        if (*sec > last_printed_sec) {
            printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
            printf("SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n", *sec, *nano, term_sec, term_nano);
            printf("--%d seconds have passed since starting\n", *sec - start_sec);
            last_printed_sec = *sec;
        }
    }

    shmdt(clock);
    return 0;
}

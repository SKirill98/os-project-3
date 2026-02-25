#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#define MAX_PCB 20
#define BILLION 1000000000

// Global variables for signal cleanup
const int BUFF_SIZE = sizeof(int) * 2;
int shm_key;
int shm_id;
int *clockptr;

// Cleanup handler for SIGINT and SIGALRM
void cleanup(int sig) {
    printf("\nOSS: Caught signal %d. Cleaning up and terminating...\n", sig);

    // Kill all children
    kill(0, SIGTERM);

    // Detach and remove shared memory
    if (clockptr != NULL)
        shmdt(clockptr);

    shmctl(shm_id, IPC_RMID, NULL);

    exit(1);
}

int main(int argc, char *argv[]) {

    int opt;
    int n = -1;          // total processes to launch
    int s = -1;          // max simultaneous
    double t = -1;       // child runtime (float seconds)
    double i = -1;       // interval between launches (float seconds)

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInSecondsToLaunchChildren]\n", argv[0]);
                printf("  -h: Display this help message and exit\n");
                printf("  -n proc   Total number of child processes to launch\n");
				        printf("  -s simul  Maximum number of children running simultaneously\n");
				        printf("  -t iter   The amount of simulated time each child runs\n");
                printf("  -i sec    Interval in simulated seconds to launch new children\n");
                return 0;
            case 'n':
                n = atoi(optarg);
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 't':
                t = atof(optarg);
                break;
            case 'i':
                i = atof(optarg);
                break;
            default:
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInSecondsToLaunchChildren]\n", argv[0]);
                printf("  -h: Display this help message and exit\n");
                printf("  -n proc   Total number of child processes to launch\n");
				        printf("  -s simul  Maximum number of children running simultaneously\n");
				        printf("  -t iter   The amount of simulated time each child runs\n");
                printf("  -i sec    Interval in simulated seconds to launch new children\n");
                return 1;
        }
    }

    if (n <= 0 || s <= 0 || t <= 0 || i < 0) {
        fprintf(stderr, "Missing or invalid required arguments\n");
        printf("Usage: %s [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInSecondsToLaunchChildren]\n", argv[0]);
        return 1;
    }

    printf("OSS starting, PID:%d PPID:%d\n", getpid(), getppid());
    printf("Called with:\n-n %d\n-s %d\n-t %.3f\n-i %.3f\n\n", n, s, t, i);

    // Setup signal handling
    signal(SIGINT, cleanup);
    signal(SIGALRM, cleanup);
    alarm(60);  // force terminate after 60 real seconds

    // Create shared memory
    int shm_key = ftok("oss.c", 'R'); // Generate a unique key for shared memory
    if (shm_key <= 0) {
        fprintf(stderr, "Parent: Failed to generate shared memory key (ftok failed)\n");
        return 1;
    }

    int shm_id = shmget(shm_key, BUFF_SIZE, 0700|IPC_CREAT); // Create shared memory segment
    if (shm_id <= 0) {
        fprintf(stderr, "Parent: Failed to create shared memory segment (shmget failed)\n");
        return 1;
    }

    clockptr = (int *)shmat(shm_id, NULL, 0); // Attach to shared memory
    if (clockptr == (void *) -1) {
        fprintf(stderr, "Parent: Failed to attach to shared memory (shmat failed)\n");
        return 1;
    }

    // Initialize shared clock
    int *sec = &clockptr[0];
    int *nano = &clockptr[1];

    *sec = 0;
    *nano = 0;

    // Convert runtime and interval to sec/nano
    int run_sec = (int)t;
    int run_nano = (int)((t - run_sec) * BILLION);

    int interval_sec = (int)i;
    int interval_nano = (int)((i - interval_sec) * BILLION);

    // PCB structure
    struct PCB {
        int occupied; // 0 for free, 1 for occupied
        pid_t pid; // Process ID of this child
        int start_sec; // Start time in seconds
        int start_nanosec; // Start time in nanoseconds
        int end_sec; // Ending time in seconds
        int end_nano; // Ending time in nanoseconds
    };

    struct PCB table[MAX_PCB];

    // Initialize table
    memset(table, 0, sizeof(table));

    int running = 0;
    int total_launched = 0;

    int last_launch_sec = 0;
    int last_launch_nano = 0;

    // Main scheduling loop
    while (total_launched < n || running > 0) {

        // Increment simulated clock
        *nano += 1000;   // 0.001ms
        if (*nano >= BILLION) {
            (*sec)++;
            *nano -= BILLION;
        }

        // Print process table every 0.5 seconds
        if (*nano % 500000000 == 0) {

            printf("\nOSS PID:%d SysClockS:%d SysClockNano:%d\n",
                   getpid(), *sec, *nano);

            printf("Entry Occupied PID StartS StartN EndS EndN\n");

            for (int j = 0; j < MAX_PCB; j++) {
                printf("%2d %8d %6d %6d %6d %6d %6d\n",
                       j,
                       table[j].occupied,
                       table[j].pid,
                       table[j].start_sec,
                       table[j].start_nanosec,
                       table[j].end_sec,
                       table[j].end_nano);
            }
        }

        // Non-blocking check for terminated child
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            running--;

            // Clear PCB entry
            for (int j = 0; j < MAX_PCB; j++) {
                if (table[j].occupied && table[j].pid == pid) {
                    table[j].occupied = 0;
                    break;
                }
            }
        }

        // Check if enough interval time has passed
        int diff_sec = *sec - last_launch_sec;
        int diff_nano = *nano - last_launch_nano;

        if (diff_nano < 0) {
            diff_sec--;
            diff_nano += BILLION;
        }

        int enough_time = 0;
        if (diff_sec > interval_sec ||
           (diff_sec == interval_sec && diff_nano >= interval_nano)) {
            enough_time = 1;
        }

        // Launch new worker if allowed
        if (running < s && total_launched < n && enough_time) {

            int index = -1;
            for (int j = 0; j < MAX_PCB; j++) {
                if (!table[j].occupied) {
                    index = j;
                    break;
                }
            }

            if (index != -1) {

                pid_t child = fork();

                if (child == 0) {
                    char secStr[20], nanoStr[20];
                    sprintf(secStr, "%d", run_sec);
                    sprintf(nanoStr, "%d", run_nano);

                    execl("./worker", "worker", secStr, nanoStr, NULL);
                    perror("execl failed");
                    exit(1);
                }

                if (child > 0) {

                    running++;
                    total_launched++;

                    last_launch_sec = *sec;
                    last_launch_nano = *nano;

                    table[index].occupied = 1;
                    table[index].pid = child;
                    table[index].start_sec = *sec;
                    table[index].start_nanosec = *nano;

                    table[index].end_sec = *sec + run_sec;
                    table[index].end_nano = *nano + run_nano;

                    if (table[index].end_nano >= BILLION) {
                        table[index].end_sec++;
                        table[index].end_nano -= BILLION;
                    }
                }
            }
        }
    }

    // Final Report
    printf("\nOSS PID:%d Terminating\n", getpid());
    printf("%d workers were launched and terminated\n", total_launched);

    // Cleanup
    shmdt(clockptr);
    shmctl(shm_id, IPC_RMID, NULL);

    return 0;
}
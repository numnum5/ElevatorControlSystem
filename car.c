#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "common.h"
#define MILLISECONDS 1000                // Number of milliseconds in a second
#define NANOSECONDS_PER_SECOND 1000000000 // Number of nanoseconds in a second
#define NANOSECONDS_PER_MILLISECOND 1000000 // Number of nanoseconds in a millisecond

// Struct for storing information about the car
typedef struct {
    char * name;
    char * lowestFloor;
    char * highestFloor;
    int delay;
    bool executeDelay;
    int sent;
    int sockfd;
    car_shared_mem * shm;
} Information;


// Global string to reference shared memory name, used for unlinking during sigint
char * shm_name;

// Function prototypes
void * receive_messages(void * arg);
void delay(car_shared_mem * shm, int value);
void * individual_operation(void * arg);
void * manual_operation(void * arg);
void * receive_messages(void * arg);
void * normal_operation(void * arg);
void waitDelay(car_shared_mem * shm, int intDelay);
void handle_sig(int sig);

int main(int argc, char ** argv) {
    // Check argument format
    if(argc != 5){
        fprintf(stderr, "./car {name} {lowest floor} {highest floor} {delay}\n");
        return 1;
    }    
    // Store the variables
    char * name = argv[1];
    char * lowestFloor = argv[2];
    char * highestFloor = argv[3];
    char * delayStr = argv[4];

    // Concatnate strings to format shared memory name
    char * shared_memory_name = concat_strings(2, "/car", name); 

    // Get delay value in ms   
    int delay = atoi(delayStr);

    // Check floor formats
    if(check_floor_format(lowestFloor) == -1 || check_floor_format(highestFloor) == -1){
        fprintf(stderr, "./car {name} {lowest floor} {highest floor} {delay}\n");
        return 1;
    }

    // Check if delay is a valid positive integer
    if(delay < 0) { 
        fprintf(stderr, "./car {name} {lowest floor} {highest floor} {delay}\n");
        return 1; 
    }
    shm_name = shared_memory_name;
    // Get access to shared memory
    car_shared_mem * shm = (car_shared_mem *)create_shared_object(shared_memory_name);

    // Initialise shared memory 
    shm->close_button = 0;
    shm->open_button = 0;
    shm->door_obstruction = 0;
    shm->overload = 0;
    shm->emergency_stop = 0;
    shm->individual_service_mode = 0;
    shm->emergency_mode = 0;
    strcpy(shm->status, "Closed");
    strcpy(shm->destination_floor, lowestFloor);
    strcpy(shm->current_floor, lowestFloor);

    // Initalise info struct with car details
    Information info;
    info.delay = delay;
    info.highestFloor = highestFloor;
    info.lowestFloor = lowestFloor;
    info.name = name;
    info.shm = shm;  
    info.sockfd = -1;

    // Threads for main elevator operation and receiving messages from controller
    pthread_t normal, manual, receive;

    // Intialise threads with p shared attributes
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_condattr_init(&cond_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED); // Set for shared use
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED); // Set for shared use
    pthread_mutex_init(&(shm->mutex), &mutex_attr);
    pthread_cond_init(&(shm->cond), &cond_attr);
    
    // Variable to store socket file descriptor
    int sockfd;

    // Create signal handler to handle sigint (Ctrl C)
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handle_sig;
    sigaction(SIGINT, &sa, NULL);

    // Initialize socket address structure for communication with controller
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);

    const char * ipaddress = "127.0.0.1";

    if (inet_pton(AF_INET, ipaddress, &addr.sin_addr) != 1) {
        fprintf(stderr, "Unable to connect to elevator system.\n");
        exit(1);
    }
    pthread_create(&normal, NULL, normal_operation, (void *)&info);
    pthread_create(&normal, NULL, manual_operation, (void *)&info);

    // Main while loop
    while(1){

        // Wait until individual or emergency mode is off
        pthread_mutex_lock(&shm->mutex);  
        while(shm->individual_service_mode == 1 || shm->emergency_mode == 1){
            pthread_cond_wait(&shm->cond, &shm->mutex);
        }
        pthread_mutex_unlock(&shm->mutex); 

        // Create socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        
        // If socket was not created sucessfully reattempt
        if(sockfd == -1){
            fprintf(stderr, "Unable to connect to elevator system.\n");
            close(sockfd);
            continue;
        }

        // Attempt to connect to the server
        if(connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) == 0){
            signal(SIGPIPE, SIG_IGN);
            // Store sockfd into info for thread functions to use
            info.sockfd = sockfd;

            // Create the recieve thread and pass the info struct
            pthread_create(&receive, NULL, receive_messages, (void *)&info);

            // Format the inital car message
            char * message = concat_strings(6, "CAR ", name, " ", lowestFloor, " ", highestFloor);
            send_message(sockfd, message);
            free(message);

            // Create buffers to hold status messages
            char buffer[25];
            char prev[25];

            pthread_mutex_lock(&shm->mutex);
            snprintf(buffer, sizeof(buffer), "STATUS %s %s %s", shm->status, shm->current_floor, shm->destination_floor);
            strcpy(prev, buffer);
            pthread_mutex_unlock(&shm->mutex);
            send_message(sockfd, buffer);

            // Maintain connection with the controller
            for(;;){
                pthread_mutex_lock(&shm->mutex);
                // Create timespec to measure measure timeout
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += (delay) / MILLISECONDS; 
                ts.tv_nsec += (delay % MILLISECONDS) * NANOSECONDS_PER_MILLISECOND;
                // Handle potential overflow
                if(ts.tv_nsec >= NANOSECONDS_PER_SECOND){
                    ts.tv_sec += ts.tv_nsec / NANOSECONDS_PER_SECOND;
                    ts.tv_nsec = ts.tv_nsec % NANOSECONDS_PER_SECOND;
                }
                // Use timedwait to either wait for change or delay elapse
                int res = pthread_cond_timedwait(&shm->cond, &shm->mutex, &ts);

                // Break out the loop if individual service mode is on
                if(shm->individual_service_mode == 1){
                    (void)send_message(sockfd, "INDIVIDUAL SERVICE");
                    pthread_mutex_unlock(&shm->mutex);
                    break;  
                }
                // Break out the loop if individual service mode is on
                if(shm->emergency_mode == 1){
                    (void)send_message(sockfd, "EMERGENCY");
                    pthread_mutex_unlock(&shm->mutex);
                    break;  
                }

                // If interrupt occurs or cond variable is signalled
                if (res == 0) {
                    // Store status message into buffer
                    snprintf(buffer, sizeof(buffer), "STATUS %s %s %s", shm->status, shm->current_floor, shm->destination_floor);
                    if (strcmp(buffer, prev) != 0) {
                        int result = send_message(sockfd, buffer);
                        if (result == -1) {
                            pthread_mutex_unlock(&shm->mutex);
                            break;  // Exit and reconnect
                        }
                        strcpy(prev, buffer);
                    }
                }
                // Send status message after delay passes
                else if(res == ETIMEDOUT){
                    snprintf(buffer, sizeof(buffer), "STATUS %s %s %s", shm->status, shm->current_floor, shm->destination_floor);
                    int result = send_message(sockfd, buffer);
                    if (result == -1) {
                        pthread_mutex_unlock(&shm->mutex);
                        break;  // Exit and reconnect
                    }
                    strcpy(prev, buffer);
                }
                pthread_mutex_unlock(&shm->mutex);;
            }
            // Cancel thread after disconnecting from the controller
            pthread_cancel(receive); 
        }
        // Clean up socket and wait a delay before reconnecting
        close(sockfd);
        shutdown(sockfd, SHUT_RDWR);
        usleep(delay * MILLISECONDS);
    }


    // Clean up
    pthread_join(normal, NULL);
    pthread_join(manual, NULL);
    pthread_cond_destroy(&shm->cond);
    pthread_mutex_destroy(&shm->mutex);
 
    free(shared_memory_name);
    shm_unlink(shared_memory_name);
    munmap((void *)shm, sizeof(car_shared_mem));

    if(shutdown(sockfd, SHUT_RDWR) == -1) {
        fprintf(stderr, "Unable to connect to elevator system.\n"); 
        return 1;
    }
    if (close(sockfd) == -1) {
        fprintf(stderr, "Unable to connect to elevator system.\n");
        return 1;
    }
    return 0;
}

/* Signal handler */
void handle_sig(int sig){
    // Unlink shared memory before exiting
    shm_unlink(shm_name);
    exit(1);
}
/* Helper function delaying a specificed amount of miliseconds */
void waitDelay(car_shared_mem * shm, int delay){
    // Create a time spec
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (delay) / MILLISECONDS; 
    ts.tv_nsec += (delay % MILLISECONDS) * NANOSECONDS_PER_MILLISECOND;
    // Handle potential overflow
    if(ts.tv_nsec >= NANOSECONDS_PER_SECOND){
        ts.tv_sec += ts.tv_nsec / NANOSECONDS_PER_SECOND;
        ts.tv_nsec = ts.tv_nsec % NANOSECONDS_PER_SECOND;
    }
    // Wait until timeing out
    while(true){
        int result = pthread_cond_timedwait(&shm->cond, &shm->mutex, &ts);
        if(result == ETIMEDOUT){
            break;
        }else if(result == 0){
            continue;
        }
    }
}

/* Function for receiving messages from the controller */
void *receive_messages(void *arg) {
    Information *info = (Information *)arg;
    car_shared_mem *shm = info->shm;
    int sockfd = info->sockfd;

    while (1) {
        char *response = receive_msg(sockfd);
        if (response == NULL) {
            break;
        }

        char newDestination[4];
        // Store new destination
        if (sscanf(response, "FLOOR %s", newDestination) != 1) {
            free(response); 
            continue; 
        }

        pthread_mutex_lock(&shm->mutex);
        // Update destination
        strcpy(shm->destination_floor, newDestination);
        // Check if the elevator is in between floors
        if (strcmp(shm->status, "Between") == 0) {
            // Wait until reaching the current destination before changing it
            while (strcmp(shm->current_floor, shm->destination_floor) != 0) {
                pthread_cond_wait(&shm->cond, &shm->mutex);
            }
            // If the new destination is the same as the current floor, open the doors
            if (strcmp(shm->current_floor, newDestination) == 0) {
                strcpy(shm->status, "Opening");
                info->executeDelay = true;
                pthread_cond_broadcast(&shm->cond);
            }
        } else if (strcmp(shm->current_floor, newDestination) == 0) {
            // If already at the destination, open the doors
            strcpy(shm->status, "Opening");
            info->executeDelay = true; 
            pthread_cond_broadcast(&shm->cond); 
        } else {
            // In all other cases, just broadcast the new destination
            pthread_cond_broadcast(&shm->cond);
        }

        pthread_mutex_unlock(&shm->mutex);
        free(response); 
    }
    return NULL;
}

/* Thread for handling main elevator operation */
void * normal_operation(void * arg){
    Information * info = (Information *)arg;
    car_shared_mem * shm = info->shm; 
    int intDelay = info->delay; 

    while(1) {
        pthread_mutex_lock(&shm->mutex);  
        // Wait for change in destination floor and status 
        while(strcmp(shm->current_floor, shm->destination_floor) == 0 && strcmp(shm->status, "Closed") == 0){
            pthread_cond_wait(&shm->cond, &shm->mutex); // Wait on condition variable
        }

        // Execute a delay if the flag is set
        if(info->executeDelay == true){
            waitDelay(shm, info->delay);
            info->executeDelay = false; // Reset delay execution flag
        }

        // If not in individual service mode, perform normal operations
        if(shm->individual_service_mode == 0){
            // Handle door opening and closing states
            if(strcmp(shm->status, "Opening") == 0){
                strcpy(shm->status, "Open"); 
                pthread_cond_broadcast(&shm->cond); 
                waitDelay(shm, info->delay); 
            }else if(strcmp(shm->status, "Closing") == 0){
                strcpy(shm->status, "Closed"); 
                pthread_cond_broadcast(&shm->cond);
                waitDelay(shm, info->delay);
            }else if(strcmp(shm->status, "Between") == 0){
                // Determine direction of travel based on floor comparison
                int compare = compare_floor(shm->current_floor, shm->destination_floor);
                if(compare == 1){
                    decrement_floor(shm->current_floor, shm->current_floor); 
                }else if(compare == -1){
                    increment_floor(shm->current_floor, shm->current_floor); 
                }
                strcpy(shm->status, "Closed"); 
                pthread_cond_broadcast(&shm->cond);
            }

            // Main state machine for the elevator's operation
            while(1){
                // If the doors are closed and at the destination floor, open doors
                if (strcmp(shm->status, "Closed") == 0){
                    if(strcmp(shm->destination_floor, shm->current_floor) == 0){
                        strcpy(shm->status, "Opening"); 
                        pthread_cond_broadcast(&shm->cond);
                        waitDelay(shm, info->delay);
                    }else{
                        strcpy(shm->status, "Between"); 
                        pthread_cond_broadcast(&shm->cond);
                        waitDelay(shm, info->delay);
                    }
                }

                // Handle Between state for moving to the destination floor
                if(strcmp(shm->status, "Between") == 0){
                    int compare = compare_floor(shm->current_floor, shm->destination_floor);
                    if(compare == 1) {
                        decrement_floor(shm->current_floor, shm->current_floor); 
                    } else if(compare == -1) {
                        increment_floor(shm->current_floor, shm->current_floor); 
                    }                                      
                    strcpy(shm->status, "Closed"); 
                    pthread_cond_broadcast(&shm->cond);
                }

                // Opening door logic
                if(strcmp(shm->status, "Opening") == 0){
                    strcpy(shm->status, "Open");
                    pthread_cond_broadcast(&shm->cond);
                    waitDelay(shm, info->delay); 
                }
                if(strcmp(shm->status, "Open") == 0){
                    strcpy(shm->status, "Closing"); 
                    pthread_cond_broadcast(&shm->cond);
                    waitDelay(shm, info->delay);
                }
                if(strcmp(shm->status, "Closing") == 0){
                    strcpy(shm->status, "Closed");
                    pthread_cond_broadcast(&shm->cond);
                    // Exit the inner while loop after closing doors
                    break; 
                }
            }     
        }
        pthread_mutex_unlock(&shm->mutex); 
    }   
}


/* Thread for handling manual operations like opening and closing doors */
void * manual_operation(void * arg){
    Information *info = (Information *)arg;
    car_shared_mem *shm = info->shm;
    int delay = info->delay;
    while(1){
        pthread_mutex_lock(&shm->mutex);
        pthread_cond_wait(&shm->cond, &shm->mutex);

        // Check if elevator is in individual service mode   
        if(shm->individual_service_mode == 1){
            // Handle buttons
            if(shm->open_button == 1){
                shm->open_button = 0; 
                strcpy(shm->status, "Opening");
                pthread_cond_broadcast(&shm->cond);
                waitDelay(shm, delay);
                strcpy(shm->status, "Open");
                pthread_cond_broadcast(&shm->cond);
            }
            if(shm->close_button == 1){
                shm->close_button = 0;
                strcpy(shm->status, "Closing");
                pthread_cond_broadcast(&shm->cond);
                waitDelay(shm, delay);
                strcpy(shm->status, "Closed"); 
                pthread_cond_broadcast(&shm->cond);
            }
            // Move elevator if destination floor differs to current floor
            if(strcmp(shm->current_floor, shm->destination_floor) != 0 && strcmp(shm->status, "Closed") == 0){
                if(check_floor_inrange(shm->destination_floor, info->highestFloor, info->lowestFloor) == false){
                    strcpy(shm->destination_floor, shm->current_floor);
                    pthread_cond_broadcast(&shm->cond);
                }else{
                    strcpy(shm->status, "Between");
                    pthread_cond_broadcast(&shm->cond);
                    waitDelay(shm, delay);
                }
            }
            if(strcmp(shm->status, "Between") == 0){
                strcpy(shm->current_floor, shm->destination_floor);
                strcpy(shm->status, "Closed");
                pthread_cond_broadcast(&shm->cond);
            }
            if(strcmp(shm->status, "Opening") == 0){
                strcpy(shm->status, "Open");
                pthread_cond_broadcast(&shm->cond); 
                waitDelay(shm, delay);
            }
            if(strcmp(shm->status, "Closing") == 0){
                strcpy(shm->status, "Closed");
                pthread_cond_broadcast(&shm->cond); 
                waitDelay(shm, delay);
            }
        }else{
            // If elevator is not in individual service mode

            // Close doors
            if(shm->close_button == 1){
                if(strcmp(shm->status, "Open") == 0){
                    strcpy(shm->status, "Closing");
                    shm->close_button = 0;
                    pthread_cond_broadcast(&shm->cond);
                } else shm->close_button = 0;
            }
            // Open doors
            if(shm->open_button == 1){
                if(strcmp(shm->status, "Closed") == 0 || strcmp(shm->status, "Closing") == 0){
                    strcpy(shm->status, "Opening");
                    shm->open_button = 0;
                    info->executeDelay = true;
                    pthread_cond_broadcast(&shm->cond);
                }else if(strcmp(shm->status, "Open") == 0){
                    waitDelay(shm, delay);
                    strcpy(shm->status, "Closing");
                    shm->open_button = 0;
                    pthread_cond_broadcast(&shm->cond);
                }else shm->open_button = 0;
            }
        }
        pthread_mutex_unlock(&shm->mutex);
    }
}

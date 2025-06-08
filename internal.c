#include <stdio.h>
#include <pthread.h>
#include "common.h"

// Internal control
int main(int argc, char ** argv){
    if(argc != 3){
        fprintf(stderr, "Requires 2 arguments.\n");
        return 1;
    }
    // Store car name and operation
    char * car_name = argv[1]; 
    char * operation = argv[2];    

    // Format the sharedmemory name
    char * shared_mem_name = concat_strings(2, "/car", car_name);

    // Get shared object from shared memory region
    car_shared_mem * shared_memory = (car_shared_mem * )get_shared_object(shared_mem_name);

    // Check if shared memory is returned
    if(shared_memory == NULL){
        fprintf(stderr, "Unable to access car %s.\n", car_name);
        free(shared_mem_name);
        return 1;
    }
    // Free name
    free(shared_mem_name);

    // Acquire mutex reference
    pthread_mutex_t * mutext_ptr = &(shared_memory->mutex);

    // Critical section
    pthread_mutex_lock(&shared_memory->mutex);
    if(strcmp(operation, "open") == 0){
        shared_memory->open_button = 1;
    }else if(strcmp(operation, "close") == 0){
        shared_memory->close_button = 1;
    }else if(strcmp(operation, "stop") == 0){
        shared_memory->emergency_stop = 1;
    }else if(strcmp(operation, "service_on") == 0){
        shared_memory->individual_service_mode = 1;
        shared_memory->emergency_mode = 0;
    }else if(strcmp(operation, "service_off") == 0){
        shared_memory->individual_service_mode = 0;
    }else if(strcmp(operation, "up") == 0 || strcmp(operation, "down") == 0) {
    // Sets the destination floor to the next floor up from the current floor. 
        if(shared_memory->individual_service_mode == 1){
            if(strcmp(shared_memory->status, "Open") == 0 
            || strcmp(shared_memory->status, "Opening")== 0 
            || strcmp(shared_memory->status, "Closing") == 0){
                // Unlock mutex before exiting
                pthread_mutex_unlock(&shared_memory->mutex);
                fprintf(stderr, "Operation not allowed while doors are open.\n");
                return 1;
            }
            else if(strcmp(shared_memory->status, "Between") == 0){
                // Unlock mutex before exiting
                pthread_mutex_unlock(&shared_memory->mutex);
                fprintf(stderr, "Operation not allowed while elevator is moving.\n");
                return 1;
            }
            if(strcmp(operation, "up") == 0){
                // Increment the currentfloor 1 floor close to destination floor
                increment_floor(shared_memory->destination_floor, shared_memory->current_floor);
            }else if(strcmp(operation, "down") == 0){
                // If operation is down
                // Decrement the currentfloor 1 floor close to the destination floor
                decrement_floor(shared_memory->destination_floor, shared_memory->current_floor);
            }
        }
        else{
            pthread_mutex_unlock(&shared_memory->mutex);
            fprintf(stderr, "Operation only allowed in service mode.\n");
            return 1;
        }
    }
    else{
        pthread_mutex_unlock(&shared_memory->mutex);
        fprintf(stderr, "Invalid operation.\n");
        return 1;
    }
    // Broadcast any changes
    pthread_cond_broadcast(&(shared_memory->cond)); 
    pthread_mutex_unlock(&shared_memory->mutex);
    return 0;
}

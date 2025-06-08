#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h> 
/*
 * MISRA C Compliance - Exceptions
 * 
 * Use of fcntl.h and sys/mman.h headers for opening shared memory is required
 * Addtionally use of pthread.h is required for use of mutex and cond variables
 * 
 * Rule 21.17
 * Use of string.h header and functions like strncmp and strncpy all have buffer limit to prevent overflow and overwrite
 * 
 * Double pointer usage, char ** argv which is used to take in command line arguments, required to get name of the car
 */

/* Struct for shared memory region */
typedef struct {
    pthread_mutex_t mutex;           
    pthread_cond_t cond;              
    char current_floor[4];            
    char destination_floor[4];        
    char status[8];                  
    uint8_t open_button;             
    uint8_t close_button;             
    uint8_t door_obstruction;         
    uint8_t overload;                 
    uint8_t emergency_stop;          
    uint8_t individual_service_mode;  
    uint8_t emergency_mode;           
} car_shared_mem;

/* Function prototype */
size_t string_length(const char * string);
uint8_t concat_two_strings(const char *first, const char *second, char *destination, size_t length);
uint8_t check_valid_status(const char *status);
uint8_t concat(char *dest, const char *src, size_t length);
uint8_t check_door_obstruction(const char * status);
uint8_t check_valid_field(uint8_t value);
uint8_t check_valid_floor_format(const char *floor);
void * get_shared_memory(const char * name);
void print_message(const char *message);

/* Safety component */
int main(int argc, char ** argv){
    /* Initialize status for a single point of exit */ 
    int status = 0; 
    if (argc != 2) {
        print_message("Requires 2 arguments.\n");
        status = 1; 
    } else {
        const char * name = argv[1];

        /* Check if name is NULL */
        if (name == NULL) {
            print_message("Name is null.\n");
            status = 1; 
        } else {
            const char * slash_car = "/car";
            /* Create a buffer with 252 byte limit and intialise it with 0 */
            char shared_memory_name[252] = {0};

            /* Concatenate the slash car and car name and check the result */
            uint8_t result = concat_two_strings(slash_car, name, shared_memory_name, 252);
            if (result == 0U) {
                print_message("Concatenation failed.\n");
                status = 1; 
            } else {
                car_shared_mem * shm = (car_shared_mem *)get_shared_memory(shared_memory_name);

                if (shm == NULL) {
                    /* If shared memory is NULL display appropriate message */
                    char buffer[256] = {0};
                    const char * access_message = "Unable to access car ";
                    const char * newline = ".\n";
                    uint8_t first = concat(buffer, access_message, 256);
                    uint8_t second = concat(buffer, name, 256);
                    uint8_t third = concat(buffer, newline, 256);

                    /* If concatenating the message fails send a message regardless */
                    if (first == 0U || second == 0U || third == 0U) {
                        print_message("Unable to access shared memory\n");
                    } else {
                        print_message(buffer);
                    }
                    status = 1;  // Set status instead of returning directly
                } else {
                    for (;;) {
                        (void)pthread_mutex_lock(&shm->mutex);
                        /* Lock the mutex and wait for change */
                        (void)pthread_cond_wait(&shm->cond, &shm->mutex);

                        /* Check if door is obstructed and door is closing */
                        if (shm->door_obstruction == 1U && strncmp(shm->status, "Closing", 8) == 0) {
                            (void)strncpy(shm->status, "Opening", 8);
                            (void)pthread_cond_broadcast(&shm->cond);
                        }

                        /* Check if emergency stop has been pressed and if emergency mode is off */
                        if (shm->emergency_stop == 1U && shm->emergency_mode == 0U) {
                            print_message("The emergency stop button has been pressed!\n");
                            shm->emergency_mode = 1U;
                            (void)pthread_cond_broadcast(&shm->cond);
                        }

                        /* Check if overload sensor has been triggered and the emergency mode is off */
                        if (shm->overload == 1U && shm->emergency_mode == 0U) {
                            print_message("The overload sensor has been tripped!\n");
                            shm->emergency_mode = 1U;
                            (void)pthread_cond_broadcast(&shm->cond);
                        }

                        if (shm->emergency_mode != 1U) {
                            /* Check all the fields in the shared memory */
                            uint8_t valid_floor_current = check_valid_floor_format(shm->current_floor);
                            uint8_t valid_floor_destination = check_valid_floor_format(shm->destination_floor);
                            uint8_t valid_status = check_valid_status(shm->status);
                            uint8_t valid_close_button = check_valid_field(shm->close_button);
                            uint8_t valid_open_button = check_valid_field(shm->open_button);
                            uint8_t valid_door_obstruction = check_valid_field(shm->door_obstruction);
                            uint8_t valid_overload = check_valid_field(shm->overload);
                            uint8_t valid_emergency_stop = check_valid_field(shm->emergency_stop);
                            uint8_t valid_individual_service_mode = check_valid_field(shm->individual_service_mode);
                            uint8_t valid_emergency_mode = check_valid_field(shm->emergency_mode);

                            /* If there is an invalid field print out error message */
                            if (valid_floor_current == 0U || valid_floor_destination == 0U || valid_status == 0U ||
                                valid_close_button == 0U || valid_open_button == 0U || valid_door_obstruction == 0U ||
                                valid_overload == 0U || valid_emergency_stop == 0U || valid_individual_service_mode == 0U ||
                                valid_emergency_mode == 0U || (shm->door_obstruction == 1U && check_door_obstruction(shm->status) == 0U)) {
                                print_message("Data consistency error!\n");
                                shm->emergency_mode = 1U;
                                (void)pthread_cond_broadcast(&shm->cond);
                            }
                        }
                        (void)pthread_mutex_unlock(&shm->mutex);
                    }
                }
            }
        }
    }
    /* Gracefully exit with return value of status */
    return status;
}

/* Function for calculating the length of a string */
size_t string_length(const char * string){
    size_t counter = 0;
    while(string[counter] != '\0'){
        counter++;
    }
    return counter;
}

void print_message(const char *message){
    size_t length = string_length(message);
    /* Use write to output the message */
    (void)write(STDOUT_FILENO, message, length);  
}

/* Function checking valid status */
uint8_t check_valid_status(const char *status) {
    uint8_t is_valid = 0U;
    if (strncmp(status, "Opening", 8) == 0 ||
        strncmp(status, "Closing", 8) == 0 ||
        strncmp(status, "Closed", 8) == 0 ||
        strncmp(status, "Open", 8) == 0 ||
        strncmp(status, "Between", 8) == 0) {
        is_valid = 1U;
    }
    return is_valid;
}

/* Function for status when door is obstructed */
uint8_t check_door_obstruction(const char * status){
    uint8_t is_valid = 0U;
    if (strncmp(status, "Opening", 8) == 0 ||
        strncmp(status, "Closing", 8) == 0) {
        is_valid = 1U;
    }
    return is_valid;
}

uint8_t check_valid_field(uint8_t value) {
    uint8_t is_valid = 0U;
    if (value == 0U || value == 1U) {
        is_valid = 1U;
    }
    return is_valid;
}

/* Function for checking valid floor format */
uint8_t check_valid_floor_format(const char *floor) {
    uint8_t is_valid = 0U; 
    size_t floor_len = string_length(floor);

    /* Check for valid floor length (1 to 3 characters) */
    if (floor_len == 1U || floor_len == 2U || floor_len == 3U) {
        /* Check for basement floors "B99" to "B1" */
        if (floor[0] == 'B') {
            if (floor_len == 2U && (floor[1] >= '1' && floor[1] <= '9')) {
                is_valid = 1U; 
            }
            else if (floor_len == 3U && (floor[1] >= '1' && floor[1] <= '9') &&
                     (floor[2] >= '0' && floor[2] <= '9')) {
                is_valid = 1U; 
            }
            else{
                is_valid = 0U;
            }
        /* Check for floors "1" to "999" */
        }else if (floor[0] >= '1' && floor[0] <= '9') {
            if (floor_len == 1U) {
                is_valid = 1U; 
            }
            else if (floor_len == 2U && (floor[1] >= '0' && floor[1] <= '9')) {
                is_valid = 1U; 
            }
            else if (floor_len == 3U && (floor[1] >= '0' && floor[1] <= '9') &&
                     (floor[2] >= '0' && floor[2] <= '9')) {
                is_valid = 1U; 
            }
            else{
                is_valid = 0U;
            }
        }
        else{
            is_valid = 0U;
        }
    }
    return is_valid;
}

/* Function getting shared memory given the name of the shared memory */
void * get_shared_memory(const char * name){
    void * exit = NULL;
    int fd = shm_open(name, O_RDWR, 438);
    if(fd == -1)
    {
        (void)close(fd);
        exit = NULL;
    }else{
        void *ptr = (void *)mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(ptr == MAP_FAILED)
        {
            exit = NULL;
        }else{
            exit = ptr;
        }
    }
    return exit;
}

/* Function for concatenating two strings together */
uint8_t concat(char *dest, const char *src, size_t length) {
    uint8_t exit = 0U;
    size_t destination_length = string_length(dest);
    size_t source_length = string_length(src);
    
    if ((destination_length + source_length + 1) > length) {
        exit = 0U;  // Not enough space to concatenate
    } else {
        (void)strncat(dest, src, (length - destination_length - 1));
        exit = 1U;
    }

    return exit;
}

/* Function for concanenating two strings */
uint8_t concat_two_strings(const char *first, const char *second, char *destination, size_t length) {
    uint8_t exit = 0U;
    /* Concatenate the first string */ 
    if (concat(destination, first, length) == 1U) {
        /* Concatenate the second string */ 
        if (concat(destination, second, length) == 1U) {
            exit = 1U;
        }
    }
    return exit;
}

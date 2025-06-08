/* A header file including struct types and functions used by multiple components */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#define STATUS_SIZE 4

typedef struct {
    pthread_mutex_t mutex;            // Locked while the contents of the structure are being accessed
    pthread_cond_t cond;              // Signalled when the contents of the structure change
    char current_floor[4];            // C string in the range of "B99" to "B1" and "1" to "999"
    char destination_floor[4];        // Same format as above
    char status[8];                   // C string indicating the elevator's status
    uint8_t open_button;              // 1 if open doors button is pressed, 0 otherwise
    uint8_t close_button;             // 1 if close doors button is pressed, 0 otherwise
    uint8_t door_obstruction;         // 1 if obstruction detected, 0 otherwise
    uint8_t overload;                 // 1 if overload detected
    uint8_t emergency_stop;           // 1 if emergency stop button has been pressed, 0 otherwise
    uint8_t individual_service_mode;  // 0 if not in service mode, 1 if in service mode
    uint8_t emergency_mode;           // 0 if not in emergency mode, 1 if in emergency mode
} car_shared_mem;


// Function prototypes
int convert_floor_int(const char * floor);
int check_floor_format(const char * floor);
int string_to_int(const char * str);
char *concat_strings(int count, ...);
void * create_shared_object(const char* share_name) ;
int send_looped(int fd, const void *buf, size_t sz);
int send_message(int fd, const char *buf);
void* get_shared_object(const char * share_name);
int recv_looped(int fd, void *buf, size_t sz);
char *receive_msg(int fd);
void increment_floor(char * destination, char * currentfloor);
void decrement_floor(char * destination, char * currentfloor);
int compare_floor(const char * firstFloor, const char * secondFloor);
bool check_floor_inrange(const char  * floor, const char * highestFloor, const char * lowestFloor);


/* Function for checking floor format */
int check_floor_format(const char * floor){
    // Get the length
    int length = strlen(floor);
    // If length exceeds 3 return -1
    if(strlen(floor) > 3){
        return -1;
    }

    // If floor is basement floor
    if(floor[0] == 'B'){
        // Check if floor is a digit
        for(int i = 1; i < length; i++){
            if(!isdigit(floor[i])){
                return -1;
            }
        }
    }else{
        for(int i = 0; i < length; i++){
            if(!isdigit(floor[i])){
                return -1;
            }
        }
    }
    return 1;
}


/* Function for converting string to int */
int string_to_int(const char * str){
    char c; 
    int i, digit, number = 0; 
    for(i = 0; i < strlen(str); i++) 
    { 
        c = str[i]; 
        if(c >= '0' && c <= '9') //to confirm it's a digit 
        { 
            digit = c - '0'; 
            number = number * 10 + digit; 
        } 
    }
    return number;
}


/* Function for concatenating strings */
char *concat_strings(int count, ...) {
    // Calculate the total length needed for the concatenated string
    int total_length = 0;
    va_list args;

    // First, find the total length of all strings
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        const char *str = va_arg(args, const char *);
        total_length += strlen(str);
    }
    va_end(args);

    // Allocate memory for the concatenated string (+1 for the null terminator)
    char *result = malloc(total_length + 1);
    if (result == NULL) {
        return NULL; // Check for memory allocation failure
    }

    // Reset the argument list to concatenate the strings
    va_start(args, count);
    result[0] = '\0'; // Initialize result as an empty string

    for (int i = 0; i < count; i++) {
        strcat(result, va_arg(args, const char *)); // Append each string to the result
    }
    va_end(args);

    return result; // Return the concatenated string
}

/* Function for creating shared memory given name */
void * create_shared_object(const char* share_name ) {
    // Remove any previous instance of the shared memory object, if it exists.
    shm_unlink(share_name);
    int fd = shm_open(share_name, O_CREAT | O_RDWR, 0666);
    if(fd == -1)
    {
        return NULL;
    }
    int check = ftruncate(fd, sizeof(car_shared_mem));
    if(check == -1)
    {
        return NULL;
    }
    // Otherwise, attempt to map the shared memory via mmap, and save the address
    void *ptr = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // // Do not alter the following semaphore initialisation code.
    if(ptr == MAP_FAILED)
    {
        return NULL;
    }
    // If we reach this point we should return true.
    return ptr;
}
/* Function for sending message to socket fd */
int send_looped(int fd, const void *buf, size_t sz)
{
    const char *ptr = buf;
    size_t remain = sz;
    while (remain > 0) {
        ssize_t sent = write(fd, ptr, remain);
        if (sent == -1) {
            return -1;
        }
        ptr += sent;
        remain -= sent;
    }
}
/* Function for sending message to socket file descriptor */
int send_message(int fd, const char *buf) {
    uint32_t len = htonl(strlen(buf));
    
    // Send message length first
    if (send_looped(fd, &len, sizeof(len)) == -1) {
        return -1; // Failure during sending length
    }
    
    // Send the actual message
    if (send_looped(fd, buf, strlen(buf)) == -1) {
        return -1; // Failure during sending the message
    }
    return 0; // Success
}

/*  Function for getting shared memory access */
void* get_shared_object(const char * share_name){
    int fd = shm_open(share_name, O_RDWR, 0666);
    if(fd == -1)
    {
        close(fd);
        return NULL;
    }
    void *ptr = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(ptr == MAP_FAILED)
    {
        return NULL;
    }
    return ptr;
}

/* Function for receiving message from socket file descriptor */
int recv_looped(int fd, void *buf, size_t sz){
    char *ptr = buf;
    size_t remain = sz;
    while (remain > 0) {
        ssize_t received = read(fd, ptr, remain);
        // If read returns -1 or 0 return -1 to indicate socket has closed or error occured
        if (received == -1) {
            return -1;
        }
        if (received == 0) {
            return -1; 
        }
        ptr += received;
        remain -= received;
    }
    return 0;
}

/* Function for receiving message from socket file descriptor */
char *receive_msg(int fd){
    uint32_t nlen;
    int first = recv_looped(fd, &nlen, sizeof(nlen));
    if(first == -1){
        return NULL;
    }
    uint32_t len = ntohl(nlen);
    if(len == 0){
        return NULL;
    }
    char *buf = malloc(len + 1);
    if(buf == NULL){
        fprintf(stderr, "Memory allocation failed.");
        exit(1);
    }
    buf[len] = '\0';
    int result = recv_looped(fd, buf, len);
    if(result == -1){
        free(buf);
        return NULL;
    }
    return buf;
}

/* Function for converting floor string to int type */
int convert_floor_int(const char * floor){
    int number = string_to_int(floor);
    if(floor[0] =='B'){
        // Negate
        number = ~number + 1;
    }
    return number;
}

/* Function for incrementing floor given current floor */
void increment_floor(char * destination, char * currentfloor){
    int currentFloorInt = convert_floor_int(currentfloor);
    int incrementedFloor = currentFloorInt + 1;
    if(currentfloor[0] == 'B'){
        // If floor is 0 set it to 1
        if(incrementedFloor == 0){
            destination[0] = '1';
            destination[1] = '\0';
        }else{
            snprintf(destination, STATUS_SIZE, "B%d", ~(incrementedFloor) +1 );
        } 
    }else{
        snprintf(destination, STATUS_SIZE, "%d", incrementedFloor);
    }
}
/* Function for decrementing floor given current floor */
void decrement_floor(char * destination, char * currentfloor){
    int currentFloorInt = convert_floor_int(currentfloor);
    int decrementedFloor = currentFloorInt - 1;
    if(currentfloor[0] == 'B'){
        snprintf(destination, STATUS_SIZE, "B%d", ~(decrementedFloor) + 1);
    }else{
        if(decrementedFloor == 0){
            strcpy(destination, "B1");
        }else snprintf(destination, STATUS_SIZE, "%d", decrementedFloor);
    }
}

/* Function for comparing two floors */
int compare_floor(const char * firstFloor, const char * secondFloor){
    int16_t firstFloorInt = convert_floor_int(firstFloor);
    int16_t secondFloorInt = convert_floor_int(secondFloor);
    // Return 1 if first floor is bigger than second floor
    if(firstFloorInt > secondFloorInt){
        return 1;
    }
    // Return -1 if first floor is smaller than second floor
    if(secondFloorInt > firstFloorInt){
        return -1;
    }
    // Return 0 if they are the same
    if(firstFloorInt == secondFloorInt){
        return 0;
    }
}

/* Function for checking if floor is in range given the highest floor and lowest floor range */
bool check_floor_inrange(const char  * floor, const char * highestFloor, const char * lowestFloor){
    if(compare_floor(floor, highestFloor) == 1){
        return false;
    }
    if(compare_floor(floor, lowestFloor) == -1){
        return false;
    }
    return true;
}
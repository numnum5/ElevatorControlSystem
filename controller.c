#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include "common.h"
#define UP 1
#define DOWN 0

typedef struct Floor {
    int floor;
    struct Floor * next;
    int direction;
} Floor;

typedef struct Car{
    char status[8];
    char currentFloor[4];
    char destinationFloor[4];
    char highestFloor[4];
    char lowestFloor[4];
    char name[252];
    Floor * head;
    int sockfd;
    struct Car * next;
} Car;

pthread_mutex_t mutex;
pthread_cond_t cond;
int listensockfd_global;
Car * carHead = NULL; 
typedef struct Connection{
    Car ** head;
    int clientfd;
} Connection;


Floor * createFloor(int floor, int direction);
Floor * insert_after(Floor *prev, int floor, int direction);
Floor * insert_floor(Floor *head, int floor, int direction);
Floor * insertAtFront(Floor* head, int floor, int direction);
Floor * dequeue(Floor * head);
Car * searchByRange2(Car * head, const char * source, const char * destination);
Car * deleteCar(Car* head, const char * name);
int insert_end(Floor ** head, int floor, int direction) ;
int get_direction(int source, int destination);
bool updateCar(Car * head, const char * current, const char * destination, const char * status, const char * name);
void * handleConnection(void * arg);
void deleteFloors(Floor* head);
void destroyCarList(Car* head);
void int_to_string_floor(int number, char *str);
void handle_sig(int sig);



void * handleConnection(void * arg){
    // Ignore SIGPIPE signals that could occur if the socket is closed unexpectedly
    signal(SIGPIPE, SIG_IGN);
    
    // Retrieve the connection info passed to the thread and acquire the mutex for shared resource access
    Connection  * info = (Connection *)arg;
    pthread_mutex_lock(&mutex);
    
    // Get the car list head and client socket descriptor
    Car * head = *(info->head);
    int sockfd = info->clientfd;
    pthread_mutex_unlock(&mutex);
    
    // Receive the initial message from the client
    char * intialMessage = receive_msg(info->clientfd);
    
    // If message is not received, close the socket and clean up memory
    if (intialMessage == NULL) {
        fprintf(stderr, "Failed to receive message.\n");
        close(sockfd);
        free(intialMessage);
        free(info);
        return NULL;
    }

    // If the message is a "CALL" request (L means CALL in this context)
    if (intialMessage[2] == 'L') {
        char source_floor[4];     
        char destination_floor[4];
        
        // Extract source and destination floor from the message
        (void)sscanf(intialMessage, "CALL %3s %3s", source_floor, destination_floor);
        free(intialMessage);

        // Search for an available car that can service the requested floors
        (void)pthread_mutex_lock(&mutex);
        Car * selectedCar = searchByRange2(*(info->head), source_floor, destination_floor);
        (void)pthread_mutex_unlock(&mutex);
        
        // If no cars are available, send "UNAVAILABLE" to the client
        if (selectedCar == NULL) {
           (void)send_message(sockfd, "UNAVAILABLE");
        } else {
            // Send the selected car's name to the client
            (void)send_message(sockfd, concat_strings(2, "CAR ", selectedCar->name));
            
            pthread_mutex_lock(&mutex);
            
            // Convert floor strings to integers
            int destinationInt = convert_floor_int(destination_floor);
            int sourceInt = convert_floor_int(source_floor);
            int callDirection = get_direction(sourceInt, destinationInt);
            int currentFloorInt = convert_floor_int(selectedCar->currentFloor);
            int destinationFloorInt = convert_floor_int(selectedCar->destinationFloor);

            // Get the reference to the car's floor queue head
            Floor * head = selectedCar->head;
            
            // If the car is between floors, check how to insert source and destination into the queue
            if (strcmp(selectedCar->status, "Between") == 0) {
                if (head != NULL && head->floor == destinationInt) {
                    // Insert floors into the queue
                    Floor * sourceInsert = insert_floor(head, sourceInt, callDirection);
                    Floor * destinationInsert = insert_floor(sourceInsert, destinationInt, callDirection);
                } else {
                    // Create a virtual floor for floor insertion logic
                    Floor virtualFloor;
                    virtualFloor.floor = convert_floor_int(selectedCar->currentFloor);
                    int desSrcDirection = get_direction(currentFloorInt, destinationFloorInt);
                    if (desSrcDirection == UP) {
                        virtualFloor.floor++;
                    } else if (desSrcDirection == DOWN) {
                        virtualFloor.floor--;
                    }

                    if (head == NULL) {
                        virtualFloor.direction = callDirection;
                    } else if (head->floor == currentFloorInt) {
                        virtualFloor.direction = head->direction;
                    } else {
                        virtualFloor.direction = get_direction(currentFloorInt, destinationFloorInt);
                    }

                    virtualFloor.next = head;
                    Floor * sourceInsert = insert_floor(&virtualFloor, sourceInt, callDirection);
                    if (sourceInsert == NULL) {
                        insert_end(&selectedCar->head, sourceInt, callDirection);
                        insert_end(&selectedCar->head, destinationInt, callDirection);
                    } else {
                        Floor * destinationInsert = insert_floor(sourceInsert, destinationInt, callDirection);
                        head = virtualFloor.next;
                    }
                }
            } else {
                // Handle cars not currently moving (i.e., stationary)
                Floor virtualFloor;
                virtualFloor.floor = currentFloorInt;
                if (head == NULL) {
                    virtualFloor.direction = callDirection;
                } else if (head->floor == currentFloorInt) {
                    virtualFloor.direction = head->direction;
                } else {
                    virtualFloor.direction = get_direction(currentFloorInt, head->floor);
                }

                virtualFloor.next = head;
                if (virtualFloor.direction == callDirection && virtualFloor.floor == sourceInt) {
                    if (strcmp(selectedCar->status, "Closed") == 0 || strcmp(selectedCar->status, "Opening") == 0 || strcmp(selectedCar->status, "Open") == 0) {
                        // Insert the source at the front of the queue if the car is stopped
                        head = insertAtFront(head, sourceInt, callDirection);
                        Floor * destinationResult = insert_floor(head, destinationInt, callDirection);
                    } else if (strcmp(selectedCar->status, "Closing") == 0) {
                        Floor * sourceResult = insert_floor(head, sourceInt, callDirection);
                        Floor * destinationResult = insert_floor(sourceResult, destinationInt, callDirection);
                    }
                } else {
                    // Insert floors at the end of the queue if the car is not servicing the current call
                    if (head == NULL) {
                        insert_end(&head, sourceInt, callDirection);
                        insert_end(&head, destinationInt, callDirection);
                    } else {
                        Floor * sourceResult = insert_floor(&virtualFloor, sourceInt, callDirection);
                        Floor * destinationResult = insert_floor(sourceResult, destinationInt, callDirection);
                        head = virtualFloor.next;
                    }
                }
            }

            // Update the car's floor queue
            selectedCar->head = head;
            char floor[4];

            // If the car needs to move to the destination, notify it
            if (selectedCar->head != NULL && destinationFloorInt != selectedCar->head->floor || currentFloorInt == selectedCar->head->floor) {
                int_to_string_floor(head->floor, floor);
                char * response = concat_strings(2, "FLOOR ", floor);
                send_message(selectedCar->sockfd, response);
                free(response);
            }
            pthread_mutex_unlock(&mutex);
        }
    } 
    // If initial message is a CAR message
    else if(intialMessage[2] == 'R'){
        char name[252];           
        char lowest_floor[4];
        char highest_floor[4];

        // Parse the initial message to extract car details
        sscanf(intialMessage, "CAR %s %3s %3s", name, lowest_floor, highest_floor);
        free(intialMessage);

        // Allocate memory for the new Car structure
        Car * newCar = (Car *)malloc(sizeof(Car));
        if(newCar == NULL){
            fprintf(stderr, "Memory Allocation Failed.");
            exit(1);
        }

        // Lock the mutex to safely update shared data
        pthread_mutex_lock(&mutex);
        strcpy(newCar->highestFloor, highest_floor);
        strcpy(newCar->lowestFloor, lowest_floor);
        strcpy(newCar->name, name);
        strcpy(newCar->currentFloor, lowest_floor);
        strcpy(newCar->destinationFloor, lowest_floor);
        newCar->sockfd = sockfd;
        newCar->head = NULL;

        // Insert the new car into the linked list of cars
        if (*(info->head) == NULL) {
            *(info->head) = newCar;
            newCar->next = NULL;
        } else {
            newCar->next = *(info->head);
            *(info->head) = newCar;
        }
        pthread_mutex_unlock(&mutex);

        // Main loop to receive and process messages
        while(1){
            char * response = receive_msg(sockfd);
            
            // Check for errors in receiving the message
            if(response == NULL){
                free(response);
                break;
            }

            // Handle individual service or emergency cases
            if(strcmp(response, "INDIVIDUAL SERVICE") == 0 || strcmp(response, "EMERGENCY") == 0){
                free(response);
                break;
            }

            char status[8];
            char currentFloor[4];
            char destinationFloor[4];
            
            // Parse the response to extract status and floor information
            sscanf(response, "STATUS %8s %3s %3s", status, currentFloor, destinationFloor);

            // Lock the mutex to safely update the car's status and floors
            pthread_mutex_lock(&mutex);
            strcpy(newCar->status, status);
            strcpy(newCar->currentFloor, currentFloor);
            strcpy(newCar->destinationFloor, destinationFloor);
            pthread_mutex_unlock(&mutex);  

            // Check if the car's status indicates that the doors are opening
            pthread_mutex_lock(&mutex);  
            if(strcmp(status, "Opening") == 0 && newCar->head != NULL){
                // Check if the current floor matches the head of the queue
                if(newCar->head->floor == convert_floor_int(currentFloor)){
                    newCar->head = dequeue(newCar->head); 
                    
                    // Remove next car if it also matches the current floor
                    if(newCar->head != NULL && newCar->head->next != NULL){
                        if(newCar->head->next->floor == (int)convert_floor_int(currentFloor)){
                            dequeue(newCar->head->next); 
                        }
                    }

                    // If there's a new destination, send the floor message
                    if(newCar->head != NULL && newCar->head->floor != convert_floor_int(destinationFloor)){
                        char floor[4];
                        int_to_string_floor(newCar->head->floor, floor);
                        char * response = concat_strings(2, "FLOOR ", floor);
                        send_message(newCar->sockfd, response);
                        free(response);
                    }   
                }  
            }
            pthread_mutex_unlock(&mutex);            
            free(response);
        }

        // Clean up: Remove the car from the list when done
        pthread_mutex_lock(&mutex);
        deleteFloors(newCar->head);
        *(info->head) = deleteCar(*(info->head), name);
        pthread_mutex_unlock(&mutex);

    }
    // Clean up socket 
    close(sockfd);
    shutdown(sockfd, SHUT_RDWR);
    free(info);
    return NULL;  
}

int main(void){
       pthread_t client_thread;

    // Create socket 
    int listensockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listensockfd == -1) {
        fprintf(stderr, "Connection failed");
        return 1;
    }
    
    // Make socket reusable 
    int opt_enable = 1;
    if (setsockopt(listensockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1) {
        fprintf(stderr, "Connection failed");
        return 1;
    }

    listensockfd_global = listensockfd; 


    // Create signal handler
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handle_sig;
    sigaction(SIGINT, &sa, NULL);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listensockfd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Connection failed");
        return 1;
    }
    if (listen(listensockfd, 20) == -1) {
        fprintf(stderr, "Connection failed");
        return 1;
    }

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    // Main loop which waits for clients to connect
    while(1){
        struct sockaddr_in clientaddr;
        socklen_t clientaddr_len = sizeof(clientaddr);
        // Accept client
        int clientfd = accept(listensockfd, (struct sockaddr * )&clientaddr, &clientaddr_len);
        // If connection unsucesfful continune to restart the process
        if (clientfd == -1) {
            close(clientfd);
            fprintf(stderr, "Connection failed");
            continue;
        }
        // Create info to store client fd and the reference to car
        Connection * info = (Connection *)malloc(sizeof(Connection));
        info->clientfd = clientfd;
        info->head = &carHead;
        // Create a detached thread for each new connection
        pthread_create(&client_thread, NULL, handleConnection, (void *)info);
        pthread_detach(client_thread);
    }
    close(listensockfd);
    shutdown(listensockfd, SHUT_RDWR);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    destroyCarList(carHead);
    return 0;
}

/* Searches car by floor range */
Car * searchByRange2(Car * head, const char * source, const char * destination){
    Car * temp = head;
    while (temp != NULL) {
        if (check_floor_inrange(source, temp->highestFloor, temp->lowestFloor) && check_floor_inrange(destination, temp->highestFloor, temp->lowestFloor)) { // Compare statuses
            return temp;
        }
        temp = temp->next; // Move to the next node
    }
    return NULL; 
}

/* Function for searching for the car given the name */
Car * searchCar(Car * head, const char * name){
    Car *temp = head;
    while (temp != NULL) {
        if (strcmp(temp->name, name) == 0) { 
            return temp; 
        }
        temp = temp->next; 
    }
    return NULL; 
}

/* Function for appending a car to a result used for getting multiple results for searching car functions */
void addToList(Car ** resultHead, Car * car) {
    Car * newCar = (Car * )malloc(sizeof(Car));
    if (newCar == NULL) {
        perror("Failed to allocate memory");
        return;
    }
    *newCar = *car;  
    newCar->next = *resultHead;  
    *resultHead = newCar;
}

/* Function for updating car given the unique identifer name and values to update to */
bool updateCar(Car * head, const char * current, const char * destination, const char * status, const char * name){
    Car * car = searchCar(head, name);
    if(car == NULL){
        return false;
    }
    strcpy(car->currentFloor, current);
    strcpy(car->destinationFloor, destination);
    strcpy(car->status, status);
}

/* Function for getting the direction of the call given the source and destination values */
int get_direction(int source, int destination){
    if(source > destination){
        return DOWN;
    }else return UP;
}


Floor * insertAtFront(Floor* head, int floor, int direction){
    // Create a new node with the given data
    Floor * new_node = createFloor(floor, direction);
    // Make the next of the new node point to the current
    new_node->next = head;
    // Return the new node as the new head of the list
    return new_node;
}


int insert_end(Floor ** head, int floor, int direction){
    // If head is NULL, create the first node
    if(*head == NULL){
        *head = createFloor(floor, direction);
        (*head)->next = NULL;
        return 1;
    }

    // Traverse to the end of the list
    Floor * current = *head;
    while(current->next != NULL){
        current = current->next;
    }

    // Create the new node
    Floor * newFloor = createFloor(floor, direction);
    current->next = newFloor;
    newFloor->next = NULL;

    return 1;
}

/* Function for creating a new floor */
Floor* createFloor(int floor, int direction) {
    Floor * newFloor = (Floor * )malloc(sizeof(Floor));
    if(newFloor == NULL){
        fprintf(stderr, "Malloc failed.");
        exit(1);
    }
    newFloor->floor = floor;
    newFloor->direction = direction;
    newFloor->next = NULL;
    return newFloor;
}
/* Function inserting a node after the given previous node */
Floor * insert_after(Floor * prev, int floor, int direction){
    Floor * newFloor = createFloor(floor, direction);
    newFloor->next = prev->next;
    prev->next = newFloor;
    return newFloor;
}


/* Function inserting floor given the floor and direction of that floor */
Floor * insert_floor(Floor * head, int floor, int direction){
    Floor *temp = head;
    Floor *prev = NULL;
    while (temp != NULL) {
        if(temp->direction == direction){
            // If floor is equal to the request floor return the floor
            if(temp->floor == floor){
                return temp;
            }
            // UP direction
            if (direction == UP) {
                // If the current floor is less than the new floor and the next direction is different, insert here
                if (temp->next != NULL && temp->next->direction != direction && temp->floor < floor){
                    Floor * newFloor = insert_after(temp, floor, direction);
                    return newFloor;
                }
                // Insert the new floor in UP direction if the next floor is greater
                if (temp->floor < floor && (temp->next == NULL || temp->next->floor > floor)){
                    Floor * newFloor = insert_after(temp, floor, direction);
                    return newFloor;
                }
            }
            // DOWN direction
            if(direction == DOWN){
                if(prev != NULL && prev->direction != direction && temp->floor < floor){
                    Floor * newFloor = insert_after(prev, floor, direction);
                    return newFloor;
                }
                if(temp->floor > floor && (temp->next == NULL || temp->next->floor < floor || temp->next->direction != direction)){
                    Floor * newFloor = insert_after(temp, floor, direction);
                    return newFloor;
                }
                // Insert the new floor in DOWN direction if the next floor is smaller
                if(temp->floor > floor && (temp->next == NULL || temp->next->floor < floor)){
                    Floor * newFloor = insert_after(temp, floor, direction);
                    return newFloor;
                }
            }
        }
        prev = temp;
        temp = temp->next;
    }
    return NULL;
}

/* Function for deleting floor list used for clean up */
void deleteFloors(Floor* head) {
    Floor* temp;
    while (head != NULL) {
        temp = head;      
        head = head->next; 
        free(temp);        
    }
}
/* Function for deleting car list used for clean up */
void destroyCarList(Car* head) {
    Car* temp;
    while (head != NULL) {
        temp = head;      
        head = head->next; 
        free(temp);       
    }
}

/* Function for converting int to string floor format */
void int_to_string_floor(int number, char *str) {
    if (number < 0) {
        // Handle basement floors (negative numbers)
        number = ~number + 1;  
        snprintf(str, 4, "B%d", number); 
    } else {
        // Handle regular floors (positive numbers)
        snprintf(str, 4, "%d", number);  
    }
}

/* Function that returns the new head */ 
Car* deleteCar(Car* head, const char * name) {
    Car* temp = head;
    Car* prev = NULL;

    // If the head node itself holds the key to be deleted
    if (temp != NULL && strcmp(temp->name, name) == 0) {
        Car* newHead = temp->next;  // Update head to the next node
        free(temp);  // Free the old head
        return newHead;  // Return the new head
    }

    // Search for the key to be deleted
    while (temp != NULL && strcmp(temp->name, name) == 0) {
        prev = temp;
        temp = temp->next;
    }

    // If key was not present in the list
    if (temp == NULL) return head;

    // Unlink the node from the linked list
    prev->next = temp->next;
    free(temp);  // Free the memory
    return head;  // Return the original head
}


// Could not finish mutli scheduling algorithm due to time constraint
/*
The algorithm is to calculate how many floor the car needs to visit before visting the from floor in order to get
the optimal car in multi scheduling. 
*/ 

int get_optimal_car(int from, int to, int callDirection, Floor * floors){
    int fromIndex = 0;
    int toIndex = 0;
    Floor * floorTemp = floors;
    while(floorTemp != NULL){
        if(floorTemp->floor >= from && floorTemp->direction == callDirection){
            break;
        }
        fromIndex++;
        floorTemp = floorTemp->next;
    }
    floorTemp = floors;
    while(floorTemp != NULL){
        if(floorTemp->floor <= from && floorTemp->direction == callDirection){
            break;
        }
        fromIndex++;
        floorTemp = floorTemp->next;
    }
    return fromIndex + toIndex;
}

/* Function for removing the node in front of the list */
Floor * dequeue(Floor * head) {
    // Check if the list is empty
    if (head == NULL) {
        return NULL;
    }
    Floor* temp = head;
    head = head->next;
    free(temp);
    return head;
}

/* Signal handler */
void handle_sig(int sig){
    /* Close server socket fd and exit */
    close(listensockfd_global);
    exit(1);
}

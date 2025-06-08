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
#include <ctype.h>
#include "common.h"

// Call component
int main(int argc, char **argv){

    // Check if there are 3 arguments
    if(argc != 3){
        fprintf(stderr, "Correct usage: ./call {source floor} {destination floor}\n");
        return 1;
    }

    // Store source from argument 
    const char * sourceFloor = argv[1];
    // Store destination from argument
    const char * destinationFloor = argv[2];

    // Check both source and destination floor formats
    if(check_floor_format(sourceFloor) != 1 || check_floor_format(destinationFloor) != 1){
        fprintf(stderr, "Invalid floor(s) specified.\n");
        return 1;
    }

    // Check if source and destination floor are the same
    if(strcmp(sourceFloor, destinationFloor) == 0){
        fprintf(stderr, "You are already on that floor!\n");
        return 1;
    }

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // Print error when unable to create a socket
    if (sockfd == -1) {
        fprintf(stderr, "Unable to connect to elevator system.\n");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);

    // Store controller IP address
    const char * ipaddress = "127.0.0.1";

    if (inet_pton(AF_INET, ipaddress, &addr.sin_addr) != 1) {
        fprintf(stderr, "Unable to connect to elevator system.\n");
        return 1;
    }

    // Attemp to connect
    if (connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Unable to connect to elevator system.\n");
        return 1;
    }

    // Format the CALL message
    char * message = concat_strings(4, "CALL ", sourceFloor, " ", destinationFloor );
    send_message(sockfd, message);
    free(message);


    // Wait for response
    char * response = receive_msg(sockfd);
    int reponse_length = strlen(response);

    // If response is CAR
    if(response[0] == 'C'){
        int bufsize = reponse_length - 4;
        char car_name[bufsize];
        sscanf(response, "CAR %s", car_name);
        fprintf(stdout, "Car %s is arriving.\n", car_name);
    }
    // If response starts with U send unavailable message
    else if(response[0] == 'U'){
        fprintf(stderr, "Sorry, no car is available to take this request.\n");
    }
    else{
        fprintf(stderr, "Something went wrong\n");
    }
    // Free any dynamically allocated memory
    free(response);

    // Clean up
    if (shutdown(sockfd, SHUT_RDWR) == -1) {
        perror("shutdown()");
        exit(1);
    }

    if (close(sockfd) == -1) {
        perror("close()");
        exit(1);
    }
    return 0;
}
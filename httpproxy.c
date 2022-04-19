#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include<fcntl.h>
#include<ctype.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h> 
#include "List.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

char* healthcheckstmt = "GET /healthcheck HTTP/1.1\r\n\r\n";

int parallel_connections = 5;
int healthcheck_frequency = 5;
int num_files_cache = 3;
int cache_size = 1024;
List clientfds;
List connections;
List healthchecks;
int num_requests = 0;
int ports = 0;
int RESPONSE_SIZE = 1000;

pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t health_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t connections_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t health_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t health_finished_cond = PTHREAD_COND_INITIALIZER;
/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}
/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port) {
  struct sockaddr_in addr;
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    err(EXIT_FAILURE, "socket error");
  }
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htons(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) {
    err(EXIT_FAILURE, "bind error");
  }
  if (listen(listenfd, 500) < 0) {
    err(EXIT_FAILURE, "listen error");
  }
  return listenfd;
}

/**
   Creates a socket for connecting to a server running on the same
   computer, listening on the specified port number.  Returns the
   socket file descriptor on success.  On failure, returns -1 and sets
   errno appropriately.
 */
int create_client_socket(uint16_t port) {
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd < 0) {
    return -1;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (connect(clientfd, (struct sockaddr*) &addr, sizeof addr)) {
    return -1;
  }
  return clientfd;
}

// void handle_connection(int connfd) {
//   // do something
//   // when done, close socket
//   close(connfd);
// }

// void * healthcheck(void * nonsense){
//   printf("healthcheck waiting\n");
//   int current_client;
//   char response[RESPONSE_SIZE];
//   pthread_cond_wait(&health_cond, &health_lock);
//   printf("healthcheck actuvates\n");
//   //call healthcheck on every server
//   for(int i = 0; i < ports; i++){
//     pthread_mutex_lock(&connections_lock);
//     current_client = front(clientfds);
//     deleteFront(clientfds);
//     send(current_client, healthcheckstmt, strlen(healthcheckstmt), 0);
//     recv(current_client, response, RESPONSE_SIZE, 0);
//     //figure out how to get info from healthcheck and store it
//     append(clientfds, current_client);
//     pthread_mutex_unlock(&connections_lock);
//   }
//   pthread_cond_signal(&health_finished_cond);
// }

void * worker(){

}

int main(int argc, char *argv[]) {
  int listenfd;
  if (argc < 2) {
    errx(EXIT_FAILURE, "wrong arguments: %s port_num", argv[0]);
  }
  
  int opt;
  ports = argc - 1;
  int ports_arr[argc];
  int index = 0;
  int proxy = -1;
  //printf("argv = %s\n", argv[2]);
  //printf("%s\n", argv[argc-1]);
  while ((opt = getopt(argc, argv, "N:R:s:m")) != -1) { //on mac getopt doesnt rearrange argv so gotta put port number at the end, when i need to do testing for invalid inputs do it on the vm
    //printf("loop\n");
    switch (opt) {
      printf("loops\n");
      if(opt == -1){
        break;
      }
      case 'N':
        parallel_connections = atoi(optarg);
        printf("parallel_connections is: %d\n",parallel_connections);
        ports = ports - 2; //this means that 2 of the arguments arent ports
        break;
      case 'R':
        healthcheck_frequency = atoi(optarg);
        printf("healthcheck_frequency is: %d\n", healthcheck_frequency);
        break;
        ports = ports - 2;
      case 's':
        num_files_cache = atoi(optarg);
        printf("num_files_cache is: %d\n", num_files_cache);
        break;
        ports = ports - 2;
      case 'm':
        cache_size = atoi(optarg);
        printf("cache_size is: %d\n", cache_size);
        break;
        ports = ports - 2;
    default:
       printf("didn't find flags\n");
   }
  }

  if (optind < argc) 
  {
    while (optind < argc){
      if(proxy == -1){
          proxy = atoi(argv[optind++]);
          ports--;
        }
      if (optind < argc){
        //if it wasnt the first port then its not the proxy port so we add it to our ports array
        ports_arr[index] = atoi(argv[optind++]);
        index++;
      }
    }
  }

  if (proxy == 0) {
    errx(EXIT_FAILURE, "invalid port number: %d", proxy);
  }
  listenfd = create_listen_socket(proxy);
  // int nonsense = 0;
  // pthread_t health = pthread_create(&health, NULL, &healthcheck, &nonsense);

  printf("creates proxy socket\n");
  pthread_t wt[parallel_connections];
  for(int i = 0; i < parallel_connections; i++){
    wt[i] = pthread_create(&wt[i], NULL, &worker, NULL);
    printf("creates thread %d\n", i);
  }

  clientfds = newList();
  for(int i = 0; i < ports; i++){
    append(clientfds, create_client_socket(ports_arr[i]));
    printf("appends clientfd %d\n", i);
  }

  connections = newList();

  printList(stdout, clientfds);

  printf("past printList\n");
  while(1) {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) {
      warn("accept error");
      continue;
    }
    pthread_mutex_lock(&list_lock);
    //add a connfd to a queue
    append(connections, connfd);
    printf("APPENDS CONNFD %d TO QUEUE\n", connfd);
    pthread_mutex_unlock(&list_lock);

    pthread_mutex_lock(&health_lock); //gonna need to grab whatever lock the worker function is using so it freezes everything so we can healthcheck
    printf("gets here\n");
    if(num_requests % healthcheck_frequency == 0){
      printf("inside if\n");
      // pthread_cond_signal(&health_cond);
      // printf("signals healthcheck\n");
      // pthread_cond_wait(&health_finished_cond, &list_lock);



      int current_client;
      char response[RESPONSE_SIZE];
      //pthread_cond_wait(&health_cond, &health_lock);
      printf("healthcheck activates\n");
      //call healthcheck on every server
      for(int i = 0; i < ports; i++){
        pthread_mutex_lock(&connections_lock);
        current_client = front(clientfds);
        deleteFront(clientfds);
        send(current_client, healthcheckstmt, strlen(healthcheckstmt), 0);
        recv(current_client, response, RESPONSE_SIZE, 0);
        printf(response); //right here
        //figure out how to get info from healthcheck and store it
        append(clientfds, current_client);
        pthread_mutex_unlock(&connections_lock);
      }

    }
    pthread_mutex_unlock(&health_lock);
    pthread_mutex_unlock(&list_lock);


  }
  return EXIT_SUCCESS;
}
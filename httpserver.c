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

//current issues: just starting healthcheck, running a put and get request at the same time, put request finishes and goes back to worker function but in curl terminal it doesnt stop (pressing any button makes it stop) it writes to log file and everything but it doesnt stop


int BUFFER = 1000;
char* PORT_CODE;
int log_flag = 0;
int threads = 5;
List connections;
pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t hasWork = PTHREAD_COND_INITIALIZER;
pthread_cond_t available_thread = PTHREAD_COND_INITIALIZER;
// pthread_mutex_t nonempty = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t test = PTHREAD_MUTEX_INITIALIZER;

//int waiting_threads;
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

int digits(int number){
  int i = 0;
  while(number > 0){
    number /= 10;
    i++;
  }
  return i;
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

void log_fail(char* body, char* code, int logfd){
  if(log_flag){
    char log_fail_statement[strlen(body) + strlen(code)];
    pthread_mutex_lock(&log_lock);
    sprintf(log_fail_statement, "FAIL\t%s\t%s\n", body, code);
    write(logfd, log_fail_statement, strlen(log_fail_statement));
    pthread_mutex_unlock(&log_lock);
  }
}

void handle_connection(int connfd, int logfd) { //working on logging 1 request correctly (after this move on to logging multiple requests, will need to write at a file offset)
  char buf[BUFFER];
  read(connfd, buf, BUFFER);
  //write(logfd, buf, BUFFER);
  char* token = strtok(buf, "\r\n\r\n");
  char* length;
  char* host;
  char* correct_host = "Host: localhost:";
  int bad = 0;
  int content_length = 69;
  char length_s[1000];
  char first_1k_of_request[1000];
  int first = 1;
  
  do //loop through request to find host and content length
  {
    if(first){
      strcpy(first_1k_of_request, token);
      first = 0;
    }
    printf("token = %s\n", token);
    if(strstr(token, "Host:") != NULL){
      host = token;
    }else if(strstr(token, "Content-Length") != NULL){
      //printf("finds Content-Length token %s\n", token);
      length = token;
      length += 16;
      content_length = atoi(length);
      sprintf(length_s, "%d", content_length);
      break;
    }
  }
  while((token = strtok(NULL ,"\r\n")));

  //printf("length = %d\n", content_length);

  char full_host[strlen(correct_host) + strlen(PORT_CODE)];
  strcpy(full_host, correct_host); //need to change up some variable names to fix warnings PLEASE COME BACK TO THIS
  strcat(full_host, PORT_CODE);
  char* req = strtok(buf, " ");
  char* file_name = strtok(NULL, " ");
  char file_name_permanent[strlen(file_name)];
  strcpy(file_name_permanent, file_name);
  char host_permanent[strlen(full_host)];
  strcpy(host_permanent, full_host);
  char* HTTPv = strtok(NULL, " ");
  char* log_host = host_permanent + 6;
  //printf("------filename %s\n", file_name);
  //printf("try to cause segfault %s\n", HTTPv);
  //printf("checkpoint 1 for filename %s\n", file_name_permanent);
  if(file_name[0] == '/'){ // get rid of / in front of filename
    //printf("does thing HERE\n");
    file_name++;
  }
  //printf("file_name = %s\n", file_name);
  //printf("comparison = %d\n", strcmp(file_name, "healthcheck"));
  //printf("checkpoint 2 for filename %s\n", file_name_permanent);
  if(strlen(file_name) > 19){ // check to make sure file name is 19 or less characters
    bad = 1;
  }
  //printf("checkpoint 3 for filename %s\n", file_name_permanent);
  for(int i = 0; i < strlen(file_name); i++){ //check to make sure filename only contains valid characters
    if((file_name[i] != '_') && (file_name[i] != '.') && (isalnum(file_name[i]) == 0)){
        bad = 1;
        break;
    }
  }
  //printf("checkpoint 4 for filename %s\n", file_name_permanent);
  if(strcmp(HTTPv, "HTTP/1.1")){ //check for correct HTTP version
      bad = 1;
    }

  //printf("checkpoint 5 for filename %s\n", file_name_permanent);
  if(!strcmp(req, "PUT")){ // check to make sure content length is integer (only necessary if request is PUT)
    for(int i = 0; i < strlen(length); i++){
      if(isdigit(length[i]) == 0){
        bad = 1;
      }
    }
  }

  if(bad == 1 || strcmp(full_host, host) != 0){ // if any of the above checks failed
    send(connfd, "HTTP/1.1 400 Bad Request\r\n\r\nBad Request\n", strlen("HTTP/1.1 400 Bad Request\r\n\r\nBad Request\n"), 0);
    log_fail(first_1k_of_request, "400", logfd);
    close(connfd);
  }else if(!strcmp(req, "GET") && !strcmp(file_name, "healthcheck")){
    pthread_mutex_lock(&log_lock);
    printf("healthcheck\n");
    char logbuffer[2100];
    int offset = 0;
    int newline = 0;
    pread(logfd, logbuffer, 2100, 5);
    int total_lines;
    int error_count = 0;

    // int x = read(logfd, logbuffer, 2100);
    // printf("x = %d\n", x);
    printf("logbuffer = %s\n", logbuffer);
    printf("----------------\n");
    while(pread(logfd, logbuffer, 2100, offset) != 0){
      int tab_count = 0;
      printf("logbuffer = %s\n", logbuffer);
      for(int i = 0; i < 2100; i++){
        //printf("logbuffer[%d] = %c\n", i, logbuffer[i]);
        if(logbuffer[i] == '\t' || logbuffer[i] == 0x09){
          tab_count++;
          printf("found tab\n");
          printf("logbuffer[%d] = %c\n", i, logbuffer[i]);
        }else if(logbuffer[i] == '\n' || logbuffer[i] == 0x0a){
          newline = i;
          offset += i+1;
          total_lines++;
          printf("found newline %d\n",i);
          printf("logbuffer[%d] = %c\n", i, logbuffer[i]);
          break;
        }
      }
      if(tab_count == 2){
        error_count++;
      }
      printf("ok amount of tabs\n");
      printf("offset = %d\n", offset);
      memset(logbuffer, 0, sizeof logbuffer);

    }
    pthread_mutex_unlock(&log_lock);
    int healthcheck_content_length = digits(error_count) + digits(total_lines) + 2;
    char healthcheck_response[1000];
    sprintf(healthcheck_response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%d\n%d\n", healthcheck_content_length ,error_count, total_lines);
    printf("healthcheck_response = %s\n", healthcheck_response);
    send(connfd, healthcheck_response, strlen(healthcheck_response), 0);
  }else if(access(file_name, F_OK) == -1 && strcmp(req, "PUT")){ // if file does not exist and not PUT
    send(connfd, "HTTP/1.1 404 File Not Found\r\n\r\nFile Not Found\n", strlen("HTTP/1.1 404 File Not Found\r\n\r\nFile Not Found\n"), 0);
    log_fail(first_1k_of_request, "404", logfd);
    close(connfd);
  }else if(access(file_name, W_OK) == -1 && !strcmp(req, "PUT") && access(file_name, F_OK) != -1){ // if PUT and can't write to file
    send(connfd, "HTTP/1.1 403 Forbidden\r\n\r\nForbidden\n", strlen("HTTP/1.1 403 Forbidden\r\n\r\nForbidden\n"), 0);
    log_fail(first_1k_of_request, "403", logfd);
    close(connfd);
  }else if(access(file_name, R_OK) == -1 && (!strcmp(req, "GET") || !strcmp(req, "HEAD"))){ // if not PUT and can't read to file
    send(connfd, "HTTP/1.1 403 Forbidden\r\n\r\nForbidden\n", strlen("HTTP/1.1 403 Forbidden\r\n\r\nForbidden\n"), 0);
    log_fail(first_1k_of_request, "403", logfd);
    close(connfd);
  }else if(!strcmp(req, "PUT")){ //if request is PUT
    //printf("PUT %s is put\n", file_name_permanent);
    int exists = 1;

    if(access(file_name, F_OK) == 0){
      exists = 0; // file exists so dont need to send created header
    }
    int fd = open(file_name, O_CREAT | O_WRONLY | O_TRUNC, 0644); //same as creat()
    //printf("opens file for PUT %s\n", file_name_permanent);
    int bytes_read;
    int total_PUT = 0;
    memset(buf, 0, sizeof buf);

    


    //char first1000[1000]; //LOGGING
    //unsigned char *first1000unsigned;
    //printf("before strcpy\n");
    if(content_length != 0){

      bytes_read = recv(connfd, buf, BUFFER, 0);
      total_PUT += bytes_read;
      //printf("total_PUT = %d\n", total_PUT);
      //printf("in if\n");
      //printf("buf = %s\n", buf);
      //strcpy(first1000, buf); //LOGGING
      //printf("first1000 = %s\n", first1000);
    }

    //printf("PUT %s recv first time\n", file_name_permanent);
    
    unsigned char *first1000unsigned = (unsigned char *)buf; //Rishikesh Raje https://stackoverflow.com/questions/41095147/converting-unsigned-char-array-to-hex
    char hex[bytes_read * 2 + 1];
    for(int i = 0; i < bytes_read; i++) //if getting errors check for issues with bytes_read
      sprintf(hex+2*i, "%.2x", first1000unsigned[i]);

    
    

    //printf("PUT %s creates log statement\n", file_name_permanent);



    int bytes_written;
    int counter = 0;
    while(counter < content_length){ //Continuously write the bytes we read into the buffer to the file specified in the request until we have written as many bytes as the content length
      bytes_written = write(fd, buf, bytes_read);
      counter += bytes_written;
      memset(buf, 0, sizeof buf);
      if(counter == content_length){
        break;
      }
      bytes_read = recv(connfd, buf, BUFFER, 0); //read from socket to buf
      total_PUT += bytes_read;
      //printf("total_PUT = %d\n", total_PUT);
    }
    
    //printf("PUT %s finishes reading\n", file_name_permanent);

    
    
    if(exists == 1){ //constructing created header
      char* a = "HTTP/1.1 201 CREATED\r\nContent Length: ";
      char* b = "\r\n\r\nCREATED\n";
      char target[strlen(a) + strlen(b) + strlen(length_s)];
      strcpy(target, a);
      strcat(target, length_s);
      strcat(target, b);
      send(connfd, target, strlen(target), 0); //response 
    }else{ //constructing OK header
      char* a = "HTTP/1.1 200 OK\r\nContent Length: ";
      char* b = "\r\n\r\nOK\n";
      char target[strlen(a) + strlen(b) + strlen(length_s)];
      strcpy(target, a);
      strcat(target, length_s);
      strcat(target, b);
      send(connfd, target, strlen(target), 0); //response 
    }

    //printf("PUT %s constructed header\n", file_name_permanent);


    if(log_flag){
      printf("gets to mutex for PUT %s\n", file_name_permanent);
      pthread_mutex_lock(&log_lock);
      printf("in log flag total_PUT = %d\n", total_PUT);
      char log_message[2000 + strlen(hex)];
      char total_put_string[1000];
      sprintf(total_put_string, "%d", total_PUT);
      sprintf(log_message, "PUT\t%s\t%s\t%s\t%s\n", file_name_permanent, log_host, total_put_string, hex);
      int log_write_length = strlen(hex) + strlen(file_name_permanent) + strlen(log_host) + strlen(total_put_string) + 8;
      write(logfd, log_message, log_write_length);
      pthread_mutex_unlock(&log_lock);
      printf("gets past mutex for PUT %s\n", file_name_permanent);
    }

    close(fd);
    memset(buf, 0, sizeof buf);
    //printf("PUT about to close connection %d\n", connfd);
    close(connfd);
    //printf("PUT closes connection %d\n", connfd);
    //printf("PUT %s done\n", file_name_permanent);


  }else if(!strcmp(req, "GET")){ // if request was GET
    //printf("GET %s\n", file_name_permanent);

    send(connfd, "HTTP/1.1 200 OK\r\n\r\n", strlen("HTTP/1.1 200 OK\r\n\r\n"), 0); //this is probably whats wrong????
    //printf("GET %s sends status code\n", file_name_permanent);

    int fd = open(file_name, O_RDONLY); //open file
    memset(buf, 0, sizeof buf);
    int bytes_read;
    int total_GET = 0;

    //printf("GET %s opens file\n", file_name_permanent);

    bytes_read = read(fd, buf, BUFFER); //doing it once outside of loop so I can grab the first 1000 bytes
    total_GET += bytes_read;
    //printf("total = %d\n", total_GET);
    //printf("GET %s reads once\n", file_name_permanent);

    unsigned char *first1000unsigned = (unsigned char *)buf; //Rishikesh Raje https://stackoverflow.com/questions/41095147/converting-unsigned-char-array-to-hex
    char hex[bytes_read * 2 + 1];
    for(int i = 0; i < bytes_read; i++){ //if getting errors check for issues with bytes_read
      
      sprintf(hex+2*i, "%.2x", first1000unsigned[i]);
      printf("i = %d %d\n", i, first1000unsigned[i]);
    }
    //printf("GET %s makes hex\n", file_name_permanent);

    

    if(bytes_read != 0){
      send(connfd, buf, bytes_read, 0);
      memset(buf, 0, sizeof buf);
    }

    //printf("GET %s sends once\n", file_name_permanent);

    while(1){
      bytes_read = read(fd, buf, BUFFER);
      total_GET += bytes_read; //Read all bytes from the file and send them to the socket, repeat as many times as necessary as the buffer might not be big enough to hold the whole file.
      //printf("total = %d\n", total_GET);
      if(bytes_read == 0){
        break;
      }
      send(connfd, buf, bytes_read, 0); //send to socket
      memset(buf, 0, sizeof buf);
    } 

    //printf("GET %s reads and sends rest of file\n", file_name_permanent);


    
    if(log_flag){
      printf("inside log flag total = %d\n", total_GET);
      printf("gets to mutex for GET %s\n", file_name_permanent);
      pthread_mutex_lock(&log_lock);
      char log_message[2000 + strlen(hex)];
      char total_get_string[1000];
      sprintf(total_get_string, "%d", total_GET);
      sprintf(log_message, "GET\t%s\t%s\t%s\t%s\n", file_name_permanent, log_host, total_get_string, hex);
      int log_write_length = strlen(hex) + strlen(file_name_permanent) + strlen(log_host) + strlen(total_get_string) + 8;
      printf("GET %s makes log statement\n", file_name_permanent);
      write(logfd, log_message, log_write_length); //waiting to write to log file so if it crashed during previous steps 
      pthread_mutex_unlock(&log_lock);
      printf("gets past mutex for GET %s\n", file_name_permanent);
    }


    close(fd); //close file
    //printf("GET about to close connection %d\n", connfd);
    close(connfd);
    //printf("GET closes connection %d\n", connfd);
    //printf("GET %s done\n", file_name_permanent);

  }else if(!strcmp(req, "HEAD")){ //if request was HEAD
    char response[1000];
    int fd = open(file_name, O_RDWR); //open file
    memset(buf, 0, sizeof buf);
    int bytes_read;
    int total;
    while(1){
      bytes_read = read(fd, buf, BUFFER); //Read all bytes from the file, repeat as many times as necessary as the buffer might not be big enough to hold the whole file.
      total += bytes_read;
      if(bytes_read == 0){
        break;
      }
      memset(buf, 0, sizeof buf);
    } 
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", total);
    send(connfd, response, strlen(response), 0);
    //char response
    char length_head[1000];
    sprintf(length_head, "%d", total);
    char log_message[2000];
    sprintf(log_message, "HEAD\t%s\t%s\t%s\n", file_name_permanent, log_host, length_head);
    int log_write_length = strlen(file_name_permanent) + strlen(log_host) + strlen(length_head) + 8;

    close(fd);
    if(log_flag){
      pthread_mutex_lock(&log_lock);
      write(logfd, log_message, log_write_length); 
      pthread_mutex_unlock(&log_lock);
    }

  }else{ //If the request isn’t HEAD, GET or PUT
    send(connfd, "HTTP/1.1 501 Not Implemented\r\n\r\nNot Implemented\n", strlen("HTTP/1.1 501 Not Implemented\r\n\r\nNot Implemented\n"), 0);
    log_fail(first_1k_of_request, "501", logfd);
  }
  close(connfd);
}

void * worker(void * logfd){
  int log_fd = *(int *) logfd;
  int error;
  int cfd;
  //printf("worker function called\n");
  printf("-----thread has entered worker function-----\n");
  while(1){
    printf("Thread enters while loop\n");
    //printf("gets in loop\n");
    //printf("before condition variable--------------\n");
    //printList(stdout, connections);
    //printf("before condition variable--------------\n");
    error = pthread_mutex_lock(&list_lock);
    if(error){
      printf("error here 1\n");
    }
    if(length(connections) == 0){
      printf("there are no connections\n");
      printf("queue: ");
      printList(stdout, connections);
      printf("\n");
      //printf("waiting for a connection--------------\n");
      //printList(stdout, connections);
      //printf("BEFORE CONDITION VARIABLE\n");
      error = pthread_cond_wait(&hasWork, &list_lock);
      printf("condition vairable signal received\n");
      if(error){
        printf("error here 6\n");
      }
      
      //printf("waiting for a connection--------------\n");
    }
    //pthread_mutex_lock(&nonempty);
    //waiting_threads--;
    //pthread_mutex_unlock(&nonempty);
    printf("queue before dequeue: ");
    printList(stdout, connections);
    printf("\n");
    cfd = front(connections);
    deleteFront(connections);
    printf("queue after dequeue: ");
    printList(stdout, connections);
    printf("\n");
    //printf("gets connection-------------------\n");
    //printList(stdout, connections);
    
    //printf("after delete\n");
    //printList(stdout, connections);
    //printf("gets connection-------------------\n");
    //printf("dequeues\n");
    error = pthread_mutex_unlock(&list_lock);
    if(error){
      printf("error here 2\n");
    }


    if(cfd != -1){
      printf("cfd isnt -1 it is %d\n", cfd);
      printf("calls handle connection on cfd: %d\n", cfd);
      //pthread_mutex_lock(&test);
      handle_connection(cfd, log_fd);
      //pthread_mutex_unlock(&test);
      //pthread_mutex_lock(&nonempty);
      //waiting_threads++;
      //pthread_mutex_unlock(&nonempty);
      pthread_mutex_lock(&list_lock);
      if(length(connections) < threads){
        pthread_cond_signal(&available_thread);
      }
      pthread_mutex_unlock(&list_lock);
      printf("finishes handle connection on cfd: %d\n", cfd);
      cfd = -1;
    }
  }
}

int main(int argc, char *argv[]) {
  int listenfd;
  int error;
  uint16_t port;

  if (argc < 2) {
    errx(EXIT_FAILURE, "not enough arguments: %s port_num -l log_file -N number of threads (in no specific order, flags are optional)", argv[0]);
  }else if(argc > 6){
    errx(EXIT_FAILURE, "too many arguments: %s port_num -l log_file -N number of threads (in no specific order, flags are optional)", argv[0]);
  }
  int opt;
  
  char* log_file;
  //printf("argv = %s\n", argv[2]);
  //printf("%s\n", argv[argc-1]);
  while ((opt = getopt(argc, argv, "N:l:")) != -1) { //on mac getopt doesnt rearrange argv so gotta put port number at the end, when i need to do testing for invalid inputs do it on the vm
    //printf("loop\n");
    switch (opt) {
      case 'l':
        log_flag = 1;
        log_file = optarg;
        //printf("log file is: %s\n",log_file);
        break;
      case 'N':
        threads = atoi(optarg);
        //printf("number of threads is: %d\n", threads);
        break;
    default:
       printf("didn't find either flags\n");
   }
  }
  //waiting_threads = threads;
  port = strtouint16(argv[argc-1]);
  if (port == 0) {
    errx(EXIT_FAILURE, "invalid port number: %s", argv[argc-1]);
  }

  listenfd = create_listen_socket(port);
  PORT_CODE = argv[argc-1];
  int logfd = -1;
  if(log_flag){
    if(access(log_file, F_OK) != -1){ //if the file already exists we dont wanna use O_CREAT bc it will override the previous contents of file
      printf("already exists\n");
      logfd = open(log_file, O_RDWR | O_APPEND, 0644);
    }else{
      //printf("creating\n");
      logfd = open(log_file, O_CREAT | O_RDWR, S_IROTH | S_IRGRP | S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH);
    }
    char logbuffer[2100];
    int offset = 0;
    int newline = 0;
    pread(logfd, logbuffer, 2100, 5);

    // int x = read(logfd, logbuffer, 2100);
    // printf("x = %d\n", x);
    printf("logbuffer = %s\n", logbuffer);
    printf("----------------\n");
    while(pread(logfd, logbuffer, 2100, offset) != 0){
      int tab_count = 0;
      printf("logbuffer = %s\n", logbuffer);
      for(int i = 0; i < 2100; i++){
        //printf("logbuffer[%d] = %c\n", i, logbuffer[i]);
        if(logbuffer[i] == '\t' || logbuffer[i] == 0x09){
          tab_count++;
          printf("found tab\n");
          printf("logbuffer[%d] = %c\n", i, logbuffer[i]);
        }else if(logbuffer[i] == '\n' || logbuffer[i] == 0x0a){
          newline = i;
          offset += i+1;
          printf("found newline %d\n",i);
          printf("logbuffer[%d] = %c\n", i, logbuffer[i]);
          break;
        }
      }
      if(tab_count < 2 || tab_count > 4){
        errx(EXIT_FAILURE, "incorrect log file format");
      }
      printf("ok amount of tabs\n");
      printf("offset = %d\n", offset);

    }

    //here we would step through log file to make sure each line only has 3 4 or 5 tab seperated entries
  }

  connections = newList();

  pthread_t wt[threads];
  for(int i = 0; i < threads; i++){
    wt[i] = pthread_create(&wt[i], NULL, &worker, &logfd);
    //printf("i = %d\n", i);
  }
  
  while(1) {
    printf("start listening for connections\n");
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) {
      warn("accept error");
      continue;
    }
    error = pthread_mutex_lock(&list_lock);
    if(error){
      printf("error here 3\n");
    }
    //add a connfd to a queue
    append(connections, connfd);
    printf("APPENDS CONNFD %d TO QUEUE\n", connfd);
    //error = pthread_mutex_unlock(&list_lock);
    //if(error){
    //  printf("error here 4\n");
    //}
    // pthread_mutex_lock(&nonempty);
    // while(waiting_threads == 0){
    //   pthread_cond_wait(&thread_finished, &nonempty); //if there are no available threads, wait for a thread to finish and signal that there are available threads
    // }
    // pthread_mutex_unlock(&nonempty);
    //pthread_mutex_lock(&list_lock);
    if(length(connections) >= threads){
      pthread_cond_wait(&available_thread, &list_lock);
    }
    pthread_mutex_unlock(&list_lock);
    //signal thread waiting in worker function that there is work to be done
    printf("signals thread that there is a connection in the queue\n");
    error = pthread_cond_signal(&hasWork);
    if(error){
      printf("error here 5\n");
    }
  }
  close(logfd);
  return EXIT_SUCCESS;
}

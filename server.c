#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <stdbool.h> // true, false
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <getopt.h>

//#define NUMTHREADS 6
#define BUFFER_SIZE 4096
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_worker = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_variable = PTHREAD_COND_INITIALIZER;
pthread_cond_t condition_worker = PTHREAD_COND_INITIALIZER;
int lflag = 0;
int log_fd; //log file descriptor
int offset = 0; //Where can a thread start writing to the file?
//int thread_id[NUMTHREADS]  = {0, 1};
int numError = 0;
int entries = 0;

//||||||||||||||||||||||||||||||QUEUE DEFINITION BEGIN||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||QUEUE DEFINITION BEGIN
// A linked list (LL) node to store a queue entry
struct QNode {
    int key;
    struct QNode* next;
};

// The queue, front stores the front node of LL and rear stores the
// last node of LL
struct Queue {
    struct QNode *front, *rear;
};

// A utility function to create a new linked list node.
struct QNode* newNode(int k)
{
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    temp->key = k;
    temp->next = NULL;
    return temp;
}

// A utility function to create an empty queue
struct Queue* createQueue()
{
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    q->front = q->rear = NULL;
    return q;
}

// The function to add a key k to q
void enQueue(struct Queue* q, int k)
{
    // Create a new LL node
    struct QNode* temp = newNode(k);

    // If queue is empty, then new node is front and rear both
    if (q->rear == NULL) {
        q->front = q->rear = temp;
        return;
    }

    // Add the new node at the end of queue and change rear
    q->rear->next = temp;
    q->rear = temp;
}

// Function to remove a key from given queue q
void deQueue(struct Queue* q)
{
    // If queue is empty, return NULL.
    if (q->front == NULL)
        return;

    // Store previous front and move front one node ahead
    struct QNode* temp = q->front;

    q->front = q->front->next;

    // If front becomes NULL, then change rear also as NULL
    if (q->front == NULL)
        q->rear = NULL;

    free(temp);
}
struct Queue* fd_q;
//||||||||||||||||||||||||||||||QUEUE DEFINITION END||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||QUEUE DEFINITION END
//||||||||||||||||||||||||||||||OBJECT DEFINITION||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||HTTPOBJECT
struct httpObject {
    /*
        Create some object 'struct' to keep track of all
        the components related to a HTTP message
        NOTE: There may be more member variables you would want to add
    */
    char method[5];         // PUT, HEAD, GET
    char filename[28];      // what is the file we are worried about
    char httpversion[9];    // HTTP/1.1
    ssize_t content_length; // example: 13
    int status_code;
    ssize_t length_log;
    uint8_t buffer[BUFFER_SIZE];
};


/*
    \brief 1. Want to read in the HTTP message/ data coming in from socket
    \param client_sockd - socket file descriptor
    \param message - object we want to 'fill in' as we read in the HTTP message
*/
//||||||||||||||||||||||||||||||VALID_FILE||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||VALID FILE
int valid_file(char *str){
  int i;
  int length;
  length = strlen(str);
  if(length > 27) {
    return 1;

  }
  for(i = 0; i < strlen(str); i++) {
    // is it alpha numeric? No? Is it - or _? no? invalidstruct Queue* fd_q
    char c = str[i];

    if((isalnum(c)) == 0) {
      if(c != '-' && c != '_'){
        return 1;
      }
    }
  }

  return 0;

}
//||||||||||||||||||||||||||||||READ RESPONSE||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||READ RESPONSE
void read_http_response(ssize_t client_sockd, struct httpObject* message) {
     /*
     * Start constructing HTTP request based off data from socket
     */
     message->status_code = 0; //RESET
     uint8_t buff[BUFFER_SIZE + 1];
     ssize_t bytes = recv(client_sockd, buff, BUFFER_SIZE, 0);
     buff[bytes] = 0; // null terminate
     printf("[+] received %ld bytes from client\n[+] response: \n", bytes);
     write(STDOUT_FILENO, buff, bytes);
     char * token;
     int count;
     message->content_length = 0;
     count = sscanf(buff, "%s /%s %s", &message->method,
                                       &message->filename,
                                       &message->httpversion);
     //what if no file name? catches wrong so redo if that happened
     if(strcmp(message->httpversion, "Host:") == 0) {
       int second_count = sscanf(buff, "%s / %s", &message->method, &message->httpversion);
       if(second_count != 2){
         message->status_code = 400;
         return;
       }
     }

     if(strcmp(&message->httpversion, "HTTP/1.1")) {
       message->status_code = 400;

       return;
     }

     if(count != 3){
       message->status_code = 400;

       return;
     }
     int flag = valid_file(message->filename);
     if(flag == 1) {

       message->status_code = 400;

       return;
     }
     if(strcmp("PUT", message->method) == 0) {
       char* rest = buff;
       while((token = strtok_r(rest, "\n", &rest))){
         count = sscanf(token, "Content-Length: %d", &message->content_length);
         if(count == 1){
           break;
         }
         //token = strtok(NULL, "\n");
       }
       if(count == 0) {
         message->status_code = 400;

       }
     }

    return;
}

//||||||||||||||||||||||||||||||CONSTRUCT RESPONSE||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||CONSTRUCT RESPONSE
char * construct_http_response(ssize_t client_sockd, struct httpObject* message) {
    printf("Constructing Response\n");
    char response[BUFFER_SIZE];
    char alt_response[BUFFER_SIZE];
    int n;
    char msg[20];
    if(message->status_code == 200){
      strcpy(msg,"OK");
    } else if (message->status_code == 201){
      strcpy(msg,"Created");
    } else if(message->status_code == 222) {

      strcpy(msg,"OK");
    }
      else if(message->status_code == 400) {
      strcpy(msg,"Bad Request");
    } else if(message->status_code == 404) {
      strcpy(msg,"Not Found");
    }

    else {
      strcpy(msg,"Internal Server Error");
      message->status_code = 500;
    }
    n = sprintf(response, "%s %d %s\r\nContent-Length: %ld\r\n\r\n",
                                       message->httpversion,
                                       message->status_code,
                                       msg,
                                       message->content_length);
    if(message->status_code == 222) {
      pthread_mutex_lock(&mutex);
      sprintf(alt_response, "%s 200 %s\r\nContent-Length: %ld\r\n\r\n%d\n%d",
                                         message->httpversion,
                                         msg,
                                         message->content_length,
                                         numError,
                                         entries);
      pthread_mutex_unlock(&mutex);
      write(client_sockd, alt_response, strlen(alt_response));
      return;
    }
    write(client_sockd, response, strlen(response));
    return;
}
//||||||||||||||||||||||||||||||PUT||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||HANDLE PUT
void handle_put(ssize_t client_sockd, struct httpObject* message){
    message->status_code = 201;
    ssize_t wrote;
    ssize_t bytes;
    uint8_t buff[BUFFER_SIZE + 1];
    int file_val = open(message->filename, O_WRONLY| O_TRUNC |O_CREAT, 0644);

    //buff[bytes] = 0; // null terminate
    do {
      bytes = recv(client_sockd, buff, BUFFER_SIZE, 0);
      wrote = write(file_val, buff, bytes);
      message->content_length -= bytes;
      if(wrote != bytes) {
        message->status_code = 500;
        break;
      }
    } while(message->content_length > 0);

    construct_http_response(client_sockd, message);
    close(file_val);
    close(client_sockd);
    return;
}
//||||||||||||||||||||||||||||||GET||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||HANDLE GET
void handle_get(ssize_t client_sockd, struct httpObject* message){
    uint8_t buff[BUFFER_SIZE + 1];
    char * response;
    ssize_t stored = 0;
    ssize_t wrote = 0;
    char healthResponse[BUFFER_SIZE];
    int file_val = open(message->filename, O_RDONLY);
    if(file_val < 0) {
      message->status_code = 404;
      message->content_length = 0;
      if(strcmp(message->filename, "healthcheck") == 0 && lflag) {
        message->status_code = 222;
      }
      construct_http_response(client_sockd, message);

    } else {
      struct stat fileStat;
      fstat(file_val, &fileStat);
      message->status_code = 200;
      message->content_length = fileStat.st_size;
      message->length_log = fileStat.st_size;
      construct_http_response(client_sockd, message);
      do {
        stored = read(file_val, message->buffer, sizeof(message->buffer));
        wrote = write(client_sockd, message->buffer, stored);
        if(wrote != stored) {
          close(client_sockd);
          return;

        }
        message->content_length -= wrote;

      } while(message->content_length > 0);

      close(file_val);
    }
    close(client_sockd);
    return;
}
//||||||||||||||||||||||||||||||HEAD||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||HANDLE HEAD
void handle_head(ssize_t client_sockd, struct httpObject* message){
    uint8_t buff[BUFFER_SIZE + 1];
    ssize_t stored = 0;
    int file_val = open(message->filename, O_RDONLY);
    if(file_val < 0) {
      message->status_code = 404;
      message->content_length = 0;
      construct_http_response(client_sockd, message);

    } else {
    stored = read(file_val, buff, sizeof(buff));
    message->status_code = 200;
    struct stat fileStat;
    fstat(file_val, &fileStat);
    message->content_length = fileStat.st_size;
    message->length_log = fileStat.st_size;
    construct_http_response(client_sockd, message);
    close(file_val);
  }
    close(client_sockd);
    return;
}
//||||||||||||||||||||||||||||||PROCESS REQUEST||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||PROCESS REQUEST
void process_request(ssize_t client_sockd, struct httpObject* message) {
    printf("Processing Request\n");

    if(message->status_code == 400) {
      construct_http_response(client_sockd, message);
      close(client_sockd);
    }
    else if(strcmp("PUT", message->method)==0) {
      handle_put(client_sockd, message);
    } else if (strcmp("GET", message->method)==0) {
      handle_get(client_sockd, message);
    } else if(strcmp("HEAD", message->method)==0) {
      handle_head(client_sockd, message);
    } else {
      message->status_code = 400;
      construct_http_response(client_sockd, message);
      close(client_sockd);
    }
    if(log_fd){
      write_log(message);
    }
    return;
}

void write_log(struct httpObject* message){
  int m;
  char log_buff[BUFFER_SIZE];
  char end_log[] = "========\n";
  int done = 0;
  char log_line[70];
  int wrote;
  int stored = 0;
  int written = 0;
  //what if it was a FAIL?
  if(message->status_code != 200 && message->status_code != 201 && message->status_code != 222) {
    //Log failure
    sprintf(log_buff, "FAIL: %s /%s %s --- response %d\n%s", message->method, message->filename, message->httpversion, message->status_code, end_log);
    done = 1;
    pthread_mutex_lock(&mutex);
    numError += 1;
    pthread_mutex_unlock(&mutex);
  } else {
    //Was this a head request? Dont build the hexs
    if(strcmp(message->method, "HEAD") == 0){
      sprintf(log_buff, "%s /%s length %d\n%s", message->method, message->filename, message->length_log, end_log);
      done = 1;
    } else {
      //It was NOT a HEAD request, do the same thing except now build the hexes before the end_log

      int file_val = open(message->filename, O_RDONLY);
      struct stat thisStat;
      fstat(file_val, &thisStat); // can now do the calculation with thisStat.st_size
      sprintf(log_buff, "%s /%s length %d\n", message->method, message->filename, thisStat.st_size);
      int new_offset = (thisStat.st_size) + 1 + strlen(log_buff) + 8;
      pthread_mutex_lock(&mutex);
      int personal_offset = offset;
      offset += new_offset;
      pthread_mutex_unlock(&mutex);
      int limit = thisStat.st_size;
      int tracer = personal_offset + strlen(log_buff);
      pwrite(log_fd, log_buff, strlen(log_buff), personal_offset);
      do{
        stored = read(file_val, log_line, sizeof(log_line));
        written = pwrite(log_fd, log_line, stored, tracer);
        tracer += stored;
        limit -= written;
      }while(limit > 0);
      close(file_val);
      pwrite(log_fd, end_log, strlen(end_log), tracer);
    }
  }
  if(done){
    int bytes = strlen(log_buff);
    pthread_mutex_lock(&mutex);
    int personal_offset = offset;
    offset += bytes;
    pthread_mutex_unlock(&mutex);
    wrote = pwrite(log_fd, log_buff, bytes, personal_offset);
  }
  pthread_mutex_lock(&mutex);
  entries += 1;
  pthread_mutex_unlock(&mutex);
  return;
}

void begin_work(ssize_t client_sockd, struct httpObject* message){
  read_http_response(client_sockd, &message);
  process_request(client_sockd, &message);

  return;
} //END work


//||||||||||||||||||||||||||||||THREAD FUNCTIONS||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||THREAD FUNCTIONS
void* worker(int notused) {
  struct httpObject message;
  int my_sock = -1;
  while(true) {
    pthread_mutex_lock(&mutex);
    //If the front is NULL wait to be signaled
    if(fd_q->front == NULL) {
      pthread_cond_wait(&condition_worker, &mutex);
    }
    //Now that we we're signaled, check again, is there something there?
    //IFF there is then take it and dequeue
    if(fd_q->front != NULL) {
      my_sock = fd_q->front->key;
      deQueue(fd_q);
    }
    //Otherwise abnormal timing, unlock and loop again
    else {
      pthread_mutex_unlock(&mutex);
      continue;
    }
    pthread_mutex_unlock(&mutex);
    //printf("[%d] got signaled to work on socket:%d\n", notused, my_sock);
    begin_work(my_sock, &message);
    /**
    pthread_mutex_lock(&mutex);
    int to_log = lflag;
    pthread_mutex_unlock(&mutex);

    if(to_log){
      write_log(&message);
    }
    **/
  }
}
//||||||||||||||||||||||||||||||THREAD FUNCTIONS||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||THREAD FUNCTIONS END

//||||||||||||||||||||||||||||||MAIN||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||MAIN
int main(int argc, char** argv) {
    /*
        Create sockaddr_in with server information
    */
    //extern int optind;
    int threadNum = 4; //default
    int nflag = 0;
    char* logname;
    char* portGiven;
    int c;
    //BEGIN parsing command line
    while((c = getopt(argc, argv, "N:l:")) != -1){
      switch(c) {
        case 'N':
          nflag = 1;
          threadNum = atoi(optarg); //This may not work if atoi cant
          break;
        case 'l':
          lflag = 1;
          logname = optarg;
          break;
        case '?':
          //validate.
          break;
      }
    }
    portGiven = argv[optind]; //should be the first element thats not an option

    //END parsing command line
    pthread_t thread[threadNum];
    //if there was a flag for the log file then open that file
    if(lflag) {
      log_fd = open(logname, O_WRONLY| O_TRUNC | O_CREAT, 0644);
    }
    char* port = portGiven; //this needs to change to the port we grab from getOpt
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(server_addr);

    /*
        Create server socket
    */
    int server_sockd = socket(AF_INET, SOCK_STREAM, 0);

    // Need to check if server_sockd < 0, meaning an error
    if (server_sockd < 0) {
        perror("socket");
    }


    int enable = 1;

    /*
        This allows you to avoid: 'Bind: Address Already in Use' error
    */
    int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    /*
        Bind server address to socket that is open
    */
    ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);

    /*
        Listen for incoming connections
    */
    ret = listen(server_sockd, 5); // 5 should be enough, if not use SOMAXCONN

    if (ret < 0) {
        return EXIT_FAILURE;
    }

    /*
        Connecting with a client
    */
    struct sockaddr client_addr;
    socklen_t client_addrlen;

    //struct httpObject message;
    fd_q = createQueue();
    //generate threads
    for(int i = 0; i < threadNum; i++) {

      pthread_create(&thread[i], NULL, &worker, i);
    }

    while (true) {

        printf("\033[0;32m[+] server is waiting...\n");

        int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
        pthread_mutex_lock(&mutex);
        enQueue(fd_q, client_sockd);
        //printf("[+] Something enqueue'd\n");
        pthread_cond_signal(&condition_worker);
        pthread_mutex_unlock(&mutex);


        //close(client_sockd);
        //sleep(1);
        // Remember errors happen
        /**
        read_http_response(client_sockd, &message);


        process_request(client_sockd, &message);
        **/
    }

    return EXIT_SUCCESS;
}

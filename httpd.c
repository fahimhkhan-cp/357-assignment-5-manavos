#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>



#define FIFO_IN "server_in"
#define FIFO_OUT "server_out"

void create_response(FILE *network, const char *method, const char *path){

    //only look in current directory, add . to beginning of bath
   char correct_path[512];
   snprintf(correct_path, sizeof(correct_path), ".%s", path);
   FILE *file = fopen(correct_path, "r"); //or malloc??


   if (file == NULL){
      fprintf(network, "HTTP/1.0 404 Not Found\r\n\r\n");
      return;

   }

   // Get file size
    struct stat file_size;
    if (stat(correct_path, &file_size) == -1) {
        perror("stat");
        return;
    }
    

    // Send HTTP response header <-- HEAD (only sends info about file)
    fprintf(network, "HTTP/1.0 200 OK\r\n");
    fprintf(network, "Content-Type: text/html\r\n");
    fprintf(network, "Content-Length: %lld\r\n", (long long) file_size.st_size);
    fprintf(network, "\r\n");
    fflush(network);
    
   
    //printf("HTTP Response:\nHTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lld\r\n\r\n", (long long) file_size.st_size);

    
    if (strcmp(method, "GET") == 0) { //GET sends header AND file content
        // Send file content
        char buffer[1024];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            //printf("content: %s", buffer);
            if (fwrite(buffer, 1, bytes, network) < bytes) { //writing file content
                perror("Client disconnected or write error");
                break; // Exit the loop if write fails
            }

        }
    }

    fclose(file);

}


void handle_request(int nfd)
{
    
   FILE *network = fdopen(nfd, "r");
   char *line = NULL;
   size_t size;
   //ssize_t num;

   if (network == NULL)
   {
      perror("fdopen");
      close(nfd);
      return;
   }

  if(getline(&line, &size, network) > 0){
    char type[16], filename[256], version[16];
    //printf("Request line: %s\n", line);
    if (sscanf(line, "%s %s %s", type, filename, version) == 3) {
        if (strcmp(filename, "/favicon.ico") == 0) { //send
                fprintf(network, "HTTP/1.0 404 Not Found\r\n");
                fprintf(network, "Content-Type: text/plain\r\n");
                fprintf(network, "Connection: close\r\n");
                fprintf(network, "\r\n");
                fflush(network);

        }
        else if (strcmp(type, "GET") == 0 || strcmp(type, "HEAD") == 0){
            create_response(network, type, filename);
        }
        else {
            fprintf(network, "HTTP/1.0 501 Not Implemented\r\n\r\n"); //not get or head
        }
    } else { //not enough info
        fprintf(network, "HTTP/1.0 400 Bad Request\r\n\r\n");
    }
    //printf("Method: %s, Path: %s, Version: %s\n", type, filename, version);

  }
  

   free(line);
   fclose(network);
}


void cgi(FILE *network, const char *program, const char *query){
   //until ./cgi-like is found
   char sanitized_program[256];
   snprintf(sanitized_program, sizeof(sanitized_program), "./cgi-like/%s", program);
   
   //error message
   if (access(sanitized_program, X_OK) != 0) {
        fprintf(network, "HTTP/1.0 404 Not Found\r\n\r\n");
        return;
    }

    char temp_file[64];
    snprintf(temp_file, sizeof(temp_file), "/tmp/cgi_output_%d", getpid());
    FILE *out = fopen(temp_file, "w+");
    if (!out) {
        fprintf(network, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) { // Child process
        dup2(fileno(out), STDOUT_FILENO);
        fclose(out);

        // Parse query into arguments
        char *args[256] = { sanitized_program };
        int i = 1;
        char *arg = strtok(query, "&");
        while (arg && i < 255) {
            args[i++] = arg;
            arg = strtok(NULL, "&");
        }
        args[i] = NULL;

        execvp(args[0], args);
        perror("execvp"); // Only reached on error
        exit(1);
    }

    fclose(out);
    waitpid(pid, NULL, 0);

    // Read and send CGI output
        struct stat file_size;
    if (stat(temp_file, &file_size) == -1) {
        perror("stat");
        return;
    }

    fprintf(network, "HTTP/1.0 200 OK\r\n");
    fprintf(network, "Content-Type: text/html\r\n");
    fprintf(network, "Content-Length: %lld\r\n", file_size.st_size);
    fprintf(network, "\r\n");

    char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), out)) > 0) {
        fwrite(buffer, 1, bytes, network);
    }

    fclose(out);
    remove(temp_file);

}





void run_service(int fd)
{
   while (1)
   {
      int nfd = accept_connection(fd);
      if (nfd != -1)
      {
       
            printf("Connection established\n");
            handle_request(nfd);
            close(nfd);
            printf("Connection closed\n");
            //exit(0);
         }
        
      }
   }


int main(int argc, char *argv[])
{
    int port = atoi(argv[1]);
    int fd = create_service(port);

   if (fd == -1)
   {
      perror(0);
      exit(1);
   }

   printf("listening on port: %d\n", port);
   run_service(fd);
   close(fd);

   return 0;
}

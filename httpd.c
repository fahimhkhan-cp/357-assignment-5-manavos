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


void create_response(int client_fd, const char *method, const char *path) {
    //simplify path
    char correct_path[512];
    snprintf(correct_path, sizeof(correct_path), ".%s", path);

    FILE *file = fopen(correct_path, "r");
    if (file == NULL) { //file not found
        dprintf(client_fd, "404 Not Found\r\n\r\n");
        return;
    }

    //get file size
    struct stat file_stat;
    if (stat(correct_path, &file_stat) == -1) {
        perror("stat");
        fclose(file);
        return;
    }

    //send headers
    dprintf(client_fd, "HTTP/1.0 200 OK\r\n");
    dprintf(client_fd, "Content-Type: text/html\r\n");
    dprintf(client_fd, "Content-Length: %lld\r\n", (long long)file_stat.st_size);
    dprintf(client_fd, "\r\n");

    // If method is GET, send the file content
    if (strcmp(method, "GET") == 0) {
        char response[1024];
        size_t bytes_read;
        while ((bytes_read = fread(response, 1, sizeof(response), file)) > 0) {
            write(client_fd, response, bytes_read);
        }
    }

    fclose(file);
}

void cgi(int client_fd, const char *program, const char *query){
     if (strstr(program, "..") != NULL) {
        dprintf(client_fd, "HTTP/1.0 400 Bad Request\r\n\r\n");
        return;
    }

   //simplify path
   char correct_path[256];
   snprintf(correct_path, sizeof(correct_path), "./cgi-like/%s", program);
   
   
   //check if file is executable
   if (access(correct_path, X_OK) != 0) {
        dprintf(client_fd, "HTTP/1.0 404 Not Found\r\n\r\n");
        return;
    }

    //make new path for temp file
    char temp_file[64];
    snprintf(temp_file, sizeof(temp_file), "/tmp/cgi_output_%d", getpid());

    FILE *temp = fopen(temp_file, "w+"); //w to write to file
    if (temp == NULL) {
        dprintf(client_fd, "404 Not Found\r\n\r\n");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) { // Child process
        dup2(fileno(temp), STDOUT_FILENO); //write output to temp file
        dup2(fileno(temp), STDERR_FILENO); //write errors (if any) to temp file

        //search for arguments
        char *args[256] = {correct_path};
        int i = 1;
        char *arg = strtok(query, "&"); 
        while (arg && i < 255) {
            args[i++] = arg;
            arg = strtok(NULL, "&");
        }
        args[i] = NULL;
        
        execvp(args[0], args); //execute program with arguments
        perror("execvp"); //error
        exit(1);
    }

    waitpid(pid, NULL, 0); //parent

    fclose(temp);
    temp = fopen(temp_file, "r"); //open temp in read mode
    if (temp == NULL) {
        dprintf(client_fd, "404 Not Found\r\n\r\n");
        return;
    }

    //send headers
    struct stat file_size;
    if (stat(temp_file, &file_size) == -1) {
        perror("stat");
        return;
    }

    dprintf(client_fd, "HTTP/1.0 200 OK\r\n");
    dprintf(client_fd, "Content-Type: text/html\r\n");
    dprintf(client_fd, "Content-Length: %lld\r\n", file_size.st_size);
    dprintf(client_fd, "\r\n");

    //send cgi output
    char response[1024];
    size_t bytes_read;
    while ((bytes_read = fread(response, 1, sizeof(response), temp)) > 0) {
        write(client_fd, response, bytes_read);
    }

    fclose(temp);
    remove(temp_file);

}


void handle_request(int nfd){
   FILE *network = fdopen(nfd, "r");
   char *line = NULL;
   size_t size;
   ssize_t num;

   if (network == NULL)
   {
      perror("fdopen");
      close(nfd);
      return;
   }

   if ((num = getline(&line, &size, network)) >= 0){

    //parse request
      char method[16], path[256], version[16];
      if (sscanf(line, "%15s %255s %15s", method, path, version) == 3){

        if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) { //get or head requests

            if (strncmp(path, "/cgi-like/", 10) == 0){ //cgi-like
                char *program = strtok(path + 10, "?");
                char *query = strtok(NULL, "?");
                cgi(nfd, program, query);
                }

            else {
                create_response(nfd, method, path); //regular request
                }
            }

          else {
            fprintf(network, "HTTP/1.0 501 Not Implemented\r\n\r\n");
            }

          }

        else { 
            fprintf(network, "HTTP/1.0 400 Bad Request\r\n\r\n");
            printf("here");
            fflush(network);

        }
        
    }

      
   free(line);
   fclose(network);
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
         printf("Connection closed\n");
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

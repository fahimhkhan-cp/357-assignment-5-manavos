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
    // create correct path
    char correct_path[512];
    snprintf(correct_path, sizeof(correct_path), ".%s", path);

    FILE *file = fopen(correct_path, "r");
    if (file == NULL) {
        dprintf(client_fd, "HTTP/1.0 404 Not Found\r\n");
        dprintf(client_fd, "Content-Type: text/plain\r\n");
        dprintf(client_fd, "\r\n");
        dprintf(client_fd, "404 Not Found\n");
        return;
    }

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

void cgi(int client_fd, const char *program, const char *query) {
    printf("entered cgi");
    char correct_path[256];
    snprintf(correct_path, sizeof(correct_path), "./cgi-like/%s", program);

    //check if program exists
    if (access(correct_path, X_OK) != 0) {
        dprintf(client_fd, "HTTP/1.0 404 Not Found\r\n\r\n");
        return;
    }

    //temporary files for output
    char temp_file[64];
    snprintf(temp_file, sizeof(temp_file), "/tmp/cgi_output_%d", getpid());
    FILE *out = fopen(temp_file, "w+");
    if (out == NULL) {
        dprintf(client_fd, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return;
    }
    fclose(out);

    pid_t pid = fork();
    if (pid == -1) { //error
        dprintf(client_fd, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
        return;

    } else if (pid == 0) { // child
        //redirecting output of program to temp file
        out = fopen(temp_file, "w"); //opening temp file
        if (!out) {
            perror("fopen");
            exit(1);
        }
        dup2(fileno(out), STDOUT_FILENO); //redirecting
        fclose(out);

        // Prepare arguments for exec
        char *args[256] = { correct_path };
        int i = 1;
        if (query) {
            char *arg = strtok(query, "&"); //seperating path by &
            while (arg && i < 255) {
                args[i++] = arg; //adding to array
                arg = strtok(NULL, "&"); //moving to next
            }
        }
        args[i] = NULL;

        execvp(args[0], args);
        perror("execvp"); //execvp error
        exit(1);
    }


    waitpid(pid, NULL, 0); //parent

    //gather output
    struct stat file_stat;
    if (stat(temp_file, &file_stat) == -1) {
        dprintf(client_fd, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
        remove(temp_file);
        return;
    }

    FILE *temp = fopen(temp_file, "r");
    if (!temp) {
        dprintf(client_fd, "HTTP/1.0 500 Internal Server Error\r\n\r\n");
        remove(temp_file);
        return;
    }

    //send headers
    dprintf(client_fd, "HTTP/1.0 200 OK\r\n");
    dprintf(client_fd, "Content-Type: text/plain\r\n");
    dprintf(client_fd, "Content-Length: %lld\r\n", (long long)file_stat.st_size);
    dprintf(client_fd, "\r\n");

    //send content
    char response[1024];
    size_t bytes_read;
    while ((bytes_read = fread(response, 1, sizeof(response), temp)) > 0) {
        write(client_fd, response, bytes_read);
    }

    fclose(temp);
    remove(temp_file);
}



void handle_request(int nfd) {
    FILE *network = fdopen(nfd, "r+");
    if (network == NULL) {
        perror("fdopen");
        close(nfd);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), network)) {
        char method[16], path[256], version[16];
        line[strcspn(line, "\r\n")] = '\0';

        if (sscanf(line, "%s %s %s", method, path, version) != 3) {
            fprintf(network, "HTTP/1.0 400 Bad Request\r\n\r\n");
            printf("here");
            fflush(network);
            break;
        }
        

        if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
            if (strncmp(path, "/cgi-like/", 10) == 0) {
                    char *program = strtok(path + 10, "?");
                    char *query = strtok(NULL, "?");
                    cgi(fileno(network), program, query);
                }

            else {
                create_response(fileno(network), method, path);
            }


            
        } else {
            // unsupported HTTP method
            fprintf(network, "HTTP/1.0 501 Not Implemented\r\n\r\n");
            fflush(network);
        }

        // stop processing if connection is closed
        if (feof(network) || ferror(network)) {
            break;
        }
    }

    fclose(network);
}





void run_service(int fd) {
    while (1) {
        int nfd = accept_connection(fd);
        if (nfd != -1) {
            printf("Connection established\n");
            handle_request(nfd);
            close(nfd);
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




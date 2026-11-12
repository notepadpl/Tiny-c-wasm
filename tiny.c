/* tiny.c - a minimal HTTP server that serves static and
 *         dynamic content with the GET method. Neither
 *         robust, secure, nor modular. Use for instructional
 *         purposes only.
 *         Dave O'Hallaron, Carnegie Mellon
 *
 *         usage: tiny <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024
#define MAXERRS 16

extern char **environ; /* the environment */

/*
 * error - wrapper for perror used for bad syscalls
 */
void error(char *msg) {
  perror(msg);
  exit(1);
}

/*
 * cerror - returns an error message to the client
 */
void cerror(FILE *stream, char *cause, char *errnum,
        char *shortmsg, char *longmsg) {
  fprintf(stream, "HTTP/1.1 %s %s\n", errnum, shortmsg);
  fprintf(stream, "Content-type: text/html\n\n");
  fprintf(stream, "<html><title>Tiny Error</title>");
  fprintf(stream, "<body bgcolor=\"ffffff\">\n");
  fprintf(stream, "%s: %s\n", errnum, shortmsg);
  fprintf(stream, "<p>%s: %s\n", longmsg, cause);
  fprintf(stream, "<hr><em>The Tiny Web server</em></body></html>\n");
  fflush(stream);
}

int main(int argc, char **argv) {
  /* variables for connection management */
  int parentfd;           /* parent socket */
  unsigned int childfd;   /* child socket */
  int portno;             /* port to listen on */
  int clientlen;          /* byte size of client's address */
  struct hostent *hostp;  /* client host info */
  char *hostaddrp;        /* dotted decimal host addr string */
  int optval;             /* flag value for setsockopt */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */

  /* variables for connection I/O */
  FILE *stream;           /* stream version of childfd */
  char buf[BUFSIZE];      /* message buffer */
  char method[BUFSIZE];   /* request method */
  char uri[BUFSIZE];      /* request uri */
  char version[BUFSIZE];  /* request version */
  char filename[BUFSIZE]; /* path derived from uri */
  char filetype[BUFSIZE]; /* file type derived from uri */
  char cgiargs[BUFSIZE];  /* cgi argument list */
  char *p;                /* temporary pointer */
  int is_static;          /* static request? */
  struct stat sbuf;       /* file status */
  int fd;                 /* static content filedes */
  int pid;                /* process id from fork */
  int wait_status;        /* status from wait */
  void *mmap_p;           /* mmap pointer */

  /* check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  /* open socket descriptor */
  parentfd = socket(AF_INET, SOCK_STREAM, 0);
  if (parentfd < 0)
    error("ERROR opening socket");

  /* allows us to restart server immediately */
  optval = 1;
  setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR,
         (const void *)&optval , sizeof(int));

  /* bind port to socket - using modern memset instead of bzero */
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = inet_addr("127.0.0.2");
  serveraddr.sin_port = htons((unsigned short)portno);
  
  if (bind(parentfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
    error("ERROR on binding");

  /* get us ready to accept connection requests */
  if (listen(parentfd, 5) < 0)
    error("ERROR on listen");

  clientlen = sizeof(clientaddr);
  while (1) {
    childfd = accept(parentfd, (struct sockaddr *) &clientaddr, (unsigned int*) &clientlen);
    if (childfd < 0)
      error("ERROR on accept");
    
    /* determine who sent the message */
    hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
              sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
      hostaddrp = inet_ntoa(clientaddr.sin_addr);
    else
      hostaddrp = inet_ntoa(*(struct in_addr *)hostp->h_addr_list[0]);
      
    /* open the child socket descriptor as a stream */
    if ((stream = fdopen(childfd, "r+")) == NULL)
      error("ERROR on fdopen");

    /* get the HTTP request line */
    if (!fgets(buf, BUFSIZE, stream)) {
      fclose(stream);
      close(childfd);
      continue;
    }
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    /* tiny only supports the GET method */
    if (strcasecmp(method, "GET")) {
      cerror(stream, method, "501", "Not Implemented",
         "Tiny does not implement this method");
      fclose(stream);
      close(childfd);
      continue;
    }

    /* read (and ignore) the HTTP headers */
    fgets(buf, BUFSIZE, stream);
    printf("%s", buf);
    while(strcmp(buf, "\r\n") && strcmp(buf, "\n")) {
      if (!fgets(buf, BUFSIZE, stream)) break;
      printf("%s", buf);
    }

    /* parse the uri */
    if (!strstr(uri, "cgi-bin")) { /* static content */
      is_static = 1;
      strcpy(cgiargs, "");
      strcpy(filename, ".");
      strcat(filename, uri);
      if (uri[strlen(uri)-1] == '/')
        strcat(filename, "index.html");
    } else { /* dynamic content - using strchr instead of index */
      is_static = 0;
      p = strchr(uri, '?');
      if (p) {
        strcpy(cgiargs, p+1);
        *p = '\0';
      } else {
        strcpy(cgiargs, "");
      }
      strcpy(filename, ".");
      strcat(filename, uri);
    }

    /* make sure the file exists */
    if (stat(filename, &sbuf) < 0) {
      cerror(stream, filename, "404", "Not found", "Tiny couldn't find this file");
      fclose(stream);
      close(childfd);
      continue; /* Restored continue to prevent crash */
    }

    /* serve static content */
    if (is_static) {
      if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
      else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
      else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpg");
      else
        strcpy(filetype, "text/plain");

      /* print response header */
      fprintf(stream, "HTTP/1.1 200 OK\n");
      fprintf(stream, "Server: Tiny Web Server\n");
      fprintf(stream, "Content-length: %d\n", (int)sbuf.st_size);
      fprintf(stream, "Content-type: %s\n", filetype);
      fprintf(stream, "\r\n");
      fflush(stream);

      /* Use mmap to return arbitrary-sized response body */
      fd = open(filename, O_RDONLY);
      mmap_p = mmap(0, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
      close(fd); /* close file descriptor after mmap */
      fwrite(mmap_p, 1, sbuf.st_size, stream);
      munmap(mmap_p, sbuf.st_size);
    } 
    /* serve dynamic content */
    else {
      /* make sure file is a regular executable file */
      if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
        cerror(stream, filename, "403", "Forbidden",
             "You are not allow to access this item");
        fclose(stream);
        close(childfd);
        continue; /* Restored continue */
      }

      setenv("QUERY_STRING", cgiargs, 1);

      /* print first part of response header */
      sprintf(buf, "HTTP/1.1 200 OK\n");
      write(childfd, buf, strlen(buf));
      sprintf(buf, "Server: Tiny Web Server\n");
      write(childfd, buf, strlen(buf));

      pid = fork();
      if (pid < 0) {
        perror("ERROR in fork");
        exit(1);
      } else if (pid > 0) { /* parent process */
        wait(&wait_status);
      } else { /* child process */
        close(0); /* close stdin */
        dup2(childfd, 1); /* map socket to stdout */
        dup2(childfd, 2); /* map socket to stderr */
        if (execve(filename, NULL, environ) < 0) {
          perror("ERROR in execve");
        }
      }
    }

    /* clean up */
    fclose(stream);
    close(childfd);
  }
}
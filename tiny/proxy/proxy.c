
#include "../csapp.h"
#include <fcntl.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void parse_full_uri(char *uri, char *filename, char *hoststr, char *portstr);
void process(int connfd);
void command(void);

// Steps:

// 1) accept the connection
// 2) read the request & check if it's on the filter list
// 3) forward the request and pass result back

// 4) Concurrency : create a thread to handle each request | multiplex IO?!

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    fd_set read_set, ready_set;

    int real_clientfd;

    // test with:
    // curl -v --proxy http://localhost:8888 http://www.dict.cn
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    listenfd = Open_listenfd(argv[1]); //line:conc:select:openlistenfd

    FD_ZERO(&read_set); /* Clear read set */                     //line:conc:select:clearreadset
    FD_SET(STDIN_FILENO, &read_set); /* Add stdin to read set */ //line:conc:select:addstdin
    FD_SET(listenfd, &read_set); /* Add listenfd to read set */  //line:conc:select:addlistenfd

    while (1)
    {
        ready_set = read_set;
        Select(listenfd + 1, &ready_set, NULL, NULL, NULL); //line:conc:select:select
        if (FD_ISSET(STDIN_FILENO, &ready_set))             //line:conc:select:stdinready
            command();                                      /* Read command line from stdin */
        if (FD_ISSET(listenfd, &ready_set))
        { //line:conc:select:listenfdready
            clientlen = sizeof(struct sockaddr_storage);
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            process(connfd);
            Close(connfd);
        }
    }
}
void sayhi(int fd)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %d\r\n", 200);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny greeting</title>");
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "Proxy: %s\r\n", user_agent_hdr);
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

int read_requesthdrs(rio_t *rp, int content_len_read)
{
    char buf[MAXLINE];

    char length[MAXLINE];
    int body_len = 0;
    char *len_header = "Content-Length: ";

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while (strcmp(buf, "\r\n"))
    { //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);

        if (content_len_read)
        {
            char *pt = strstr(buf, len_header);
            if (pt)
            {
                strcpy(length, pt + strlen(len_header));

                body_len = atoi(length);
                printf(">>> Body length : %d \n", body_len);
            }
        }
    }
    return body_len;
}

void parse_full_uri(char *uri, char *filename, char *hostname, char *portstr)
{

    char *scheme = "http://";
    char *pSch = strstr(uri, scheme);
    if (pSch)
    {
        char *ptr = pSch + ((int)sizeof(scheme) - 1);

        int i = 0;
        while (*ptr != ':')
            hostname[i++] = *ptr++;

        hostname[i] = '\0';

        ptr++;
        i = 0;
        while (*ptr != '/')
            portstr[i++] = *ptr++;
        portstr[i] = '\0';

        char domain[BUFSIZ];
        sprintf(domain, "%s:%s", hostname, portstr);

        char *psrc = strrchr(uri, '/');
        if (psrc)
        {
            sprintf(filename, ".%s", psrc);
        }
        else
        {
            strcpy(filename, "/");
        }

        printf("parsed host : %s, port : %s, partial uri: %s\n", hostname, portstr, filename);
    }
    else
    {
        app_error("http schema not found");
    }
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    if (!strstr(uri, "cgi-bin"))
    { /* Static content */                  //line:netp:parseuri:isstatic
        strcpy(cgiargs, "");                //line:netp:parseuri:clearcgi
        strcpy(filename, ".");              //line:netp:parseuri:beginconvert1
        strcat(filename, uri);              //line:netp:parseuri:endconvert1
        if (uri[strlen(uri) - 1] == '/')    //line:netp:parseuri:slashcheck
            strcat(filename, "index.html"); //line:netp:parseuri:appenddefault
        return 1;
    }
    else
    { /* Dynamic content */    //line:netp:parseuri:isdynamic
        ptr = index(uri, '?'); //line:netp:parseuri:beginextract
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
        {
            // none op.
            //strcpy(cgiargs, "");                         //line:netp:parseuri:endextract
        }
        strcpy(filename, "."); //line:netp:parseuri:beginconvert2
        strcat(filename, uri); //line:netp:parseuri:endconvert2
        return 0;
    }
}

void process(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))
        return;

    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET"))
    {
        // TODO: support other methods beside "GET"
        clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
        return;
    }
    else
    {
        // GET
        printf(">>> reading request header begins\n");
        read_requesthdrs(&rio, 0);
        printf(">>> reading request header ends\n");
    }

    char hostname[BUFSIZ];
    char portstr[64];
    char srcname[BUFSIZ];

    parse_full_uri(uri, srcname, hostname, portstr);

    char fwdbuf[RIO_BUFSIZE];

    // filter the partial uri path http://localhost:12345/index.html
    sprintf(fwdbuf, "GET %s HTTP/1.1\r\n", strrchr(uri, '/'));
    sprintf(fwdbuf, "%sHost: %s:%s\r\n", fwdbuf, hostname, portstr);
    sprintf(fwdbuf, "%sUser-Agent: %s", fwdbuf, user_agent_hdr);
    sprintf(fwdbuf, "%sAccept: */*\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", fwdbuf);

    // forward the request
    int real_clntfd = Open_clientfd(hostname, portstr);
    Rio_writen(real_clntfd, fwdbuf, sizeof(fwdbuf));

    rio_t resp;
    Rio_readinitb(&resp, real_clntfd);

    // cache file content
    int ofd = Open("out", O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);

    // read & write back continuously
    int n;
    while ((n = Rio_readlineb(&resp, buf, MAXLINE)) != 0)
    {
        // printf("proxy received %d bytes\n", n);
        Rio_writen(ofd, buf, n);
        Rio_writen(fd, buf, n);
    }

    close(ofd);
}

void command(void)
{
    char buf[MAXLINE];
    if (!Fgets(buf, MAXLINE, stdin))
        exit(0);       /* EOF */
    printf("%s", buf); /* Process the input command */
}
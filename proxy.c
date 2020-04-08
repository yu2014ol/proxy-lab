#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3";

void *thread(void *varg);

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char *host;
  char *port;
  char path[MAXLINE];
  rio_t rio;
  char str[][20] = {"Host", "User-Agent", "Connection", "Proxy-Connection"};
  char read_request_buf[MAXLINE];
  int server_fd;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  if (!Rio_readlineb(&rio, buf, MAXLINE))  // line:netp:doit:readrequest
    return;
  if (sscanf(buf, "%s %s %s", method, uri, version) <= 0) {
    printf("input request error");
    return;
  }

  // 分割URL字符串
  host = strtok(&uri[7], "/");
  strcpy(path, "/");
  char *path_s;
  if ((path_s = strtok(NULL, ""))) {
    strcat(path, path_s);
  }
  host = strtok(host, ":");
  port = strtok(NULL, "");
  // 组成请求报头
  sprintf(buf, "%s %s HTTP/1.0\r\n", method, path);
  Rio_readlineb(&rio, read_request_buf, MAXLINE);
  while (strcmp(read_request_buf, "\r\n")) {
    char *beg = strtok(read_request_buf, ":");
    int found = 0;
    for (int i = 0; i < 4; i++) {
      if (!strcmp(beg, str[i])) {
        found = 1;
        if (!strcmp(str[i], str[0])) {
          char rest[100];
          sprintf(rest, "%s:%s", beg, strtok(NULL, ""));
          sprintf(buf, "%s%s", buf, rest);
          memset(str[0], 0, 20);
        }
        break;
      }
    }
    if (!found) {
      char rest[100];
      sprintf(rest, "%s:%s", beg, strtok(NULL, ""));
      sprintf(buf, "%s%s", buf, rest);
      memset(str[0], 0, 20);
    }
    Rio_readlineb(&rio, read_request_buf, MAXLINE);
  }
  if (*str[0]) {
    sprintf(buf, "%sHost: %s\r\n", buf, host);
  }
  sprintf(buf, "%sUser-Agent: %s\r\n", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  sprintf(buf, "%s\r\n", buf);
  // printf("%s", buf);
  // printf("-------------------");

  // 代理向服务器发送请求报文
  if (port != NULL) {
    server_fd = Open_clientfd(host, port);
  } else {
    server_fd = Open_clientfd(host, "80");
  }
  Rio_writen(server_fd, buf, strlen(buf));

  // 读取服务器响应
  int read_num;
  while ((read_num = Rio_readn(server_fd, buf, MAXLINE))) {
    Rio_writen(fd, buf, read_num);
  }
  Close(server_fd);
}
/* $end doit */

int main(int argc, char **argv) {
  int listenfd;
  int *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  Signal(SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Pthread_create(&tid, NULL, thread, connfd);
  }
}

void *thread(void *varg) {
  int connfd = *((int *)varg);
  Pthread_detach(Pthread_self());
  Free(varg);
  doit(*connfd);
  Close(connfd);
  return NULL;
}
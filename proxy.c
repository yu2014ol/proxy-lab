#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3";

int readcnt = 0;
sem_t mutex, w;
typedef struct {
  char *buf;
  char *uri;
  int use;
  int end;
} cache_struct;
cache_struct cache[10];

int reader(char *uri, int fd);
void writer(void);
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
  char uri_save[MAXLINE];

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  if (!Rio_readlineb(&rio, buf, MAXLINE))  // line:netp:doit:readrequest
    return;
  if (sscanf(buf, "%s %s %s", method, uri, version) <= 0) {
    printf("input request error");
    return;
  }
  sprintf(uri_save, "%s", uri);
  if (reader(uri_save, fd) != 0) {
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
  rio_t rio_server;
  Rio_readinitb(&rio_server, server_fd);
  int object_size = 0;  // 缓存大小
  char *buf_save =
      (char *)Malloc(MAX_OBJECT_SIZE * sizeof(char));  // Web对象缓存
  char exceed = 0;  // 超过单个缓存区最大限制标志
  while ((read_num = Rio_readnb(&rio_server, buf, MAXLINE))) {
    Rio_writen(fd, buf, read_num);
    object_size += read_num;
    if (object_size <= MAX_OBJECT_SIZE) {
      int beg = object_size - read_num;
      for (int i = 0; i < read_num; ++i) {
        buf_save[beg++] = buf[i];
      }
    } else {
      exceed = 1;
    }
  }

  // writer,要将rio函数与临界区分离，不然并发测试不通过
  // 所以先写到客户端，再将缓存的数据放到cache中
  P(&w);
  if (!exceed) {
    cache_struct *object;
    for (int i = 0; i < 10; ++i) {
      if (cache[i].use == 0) {
        object = &cache[i];
        object->use = 1;
        break;
      }
    }
    if (object == NULL) { // 随机替换
      int r = rand() % 10;
      object = &cache[r];
    }
    sprintf(object->uri, "%s", uri_save);
    for (int i = 0; i < object_size; ++i) {
      object->buf[i] = buf_save[i];
    }
    object->end = object_size;
  }
  V(&w);
  Close(server_fd);
}
/* $end doit */

int main(int argc, char **argv) {
  int listenfd;
  int *connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  Sem_init(&w, 0, 1);
  Sem_init(&mutex, 0, 1);
  for (int i = 0; i < 10; ++i) {
    cache[i].buf = (char *)Malloc(MAX_OBJECT_SIZE);
    cache[i].uri = (char *)Malloc(MAXLINE * sizeof(char));
    cache[i].use = 0;
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
  doit(connfd);
  Close(connfd);
  return NULL;
}

int reader(char *uri, int fd) {
  int flag = -1;
  P(&mutex);
  readcnt++;
  if (readcnt == 1) {
    P(&w);
  }
  V(&mutex);

  for (int i = 0; i < 10; ++i) {
    if (!strcmp(cache[i].uri, uri)) {
      flag = i;
      break;
    }
  }
  if (flag != -1) {
    Rio_writen(fd, cache[flag].buf, cache[flag].end);
  }

  P(&mutex);
  readcnt--;
  if (readcnt == 0) {
    V(&w);
  }
  V(&mutex);
  return flag == -1 ? 0 : 1;
}
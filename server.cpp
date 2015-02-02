#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <event.h>
#include <semaphore.h>

#define PORT 8080
#define ARR_SIZE 100

static int aindex = 0;
int *arr;

//ipc control
sem_t sem_fight;

typedef struct {
	int fd;
	struct bufferevent *buf_ev;
}client;

static int build_socket() {
	int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	assert(listen_socket >= 0);

	struct sockaddr_in listen_addr;
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(PORT);

	assert(bind(listen_socket, (const struct sockaddr *)(&listen_addr), sizeof(listen_addr)) >= 0);

	assert(listen(listen_socket, 5) >= 0);

	return listen_socket;
}

void read_cb(struct bufferevent *bev, void *arg) {
	char *req = evbuffer_readline(bev->input);
	if (req == NULL) return ;

	client *c = (client *)arg;
	struct evbuffer *evreturn = evbuffer_new();
	evbuffer_add_printf(evreturn, "you said %s\n", req);
	bufferevent_write_buffer(bev, evreturn);
	evbuffer_free(evreturn);
	free(req);

	arr[c->fd] = 1;
	sem_post(&sem_fight);
}

void error_cb(struct bufferevent *bev, short what, void *arg) {
	client *c = (client *)arg;
	bufferevent_free(c->buf_ev);
	close(c->fd);
	free(c);
}

void accept_callback(int fd, short ev, void *arg) {
	int client_fd;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);

	client_fd = accept(fd, (struct sockaddr *)(&client_addr), &client_len);
	assert(client_fd >= 0);

	client *c = (client *)calloc(1, sizeof(*c));
	assert(c != NULL);

	c->fd = client_fd;
	c->buf_ev = bufferevent_new(client_fd, read_cb, NULL, error_cb, c);
	bufferevent_enable(c->buf_ev, EV_READ);
	printf("accept_callback, pid=%d\n", getpid());
}

void work_as_child() {
	printf("child speaking:pid=%d\n", getpid());
	sleep(5);

	for (int i=0; i<ARR_SIZE; ++i)
		printf("child arr %d is %d\n", i, arr[i]);

	while(true) {
		for (int i=0; i<ARR_SIZE; ++i)
			if (arr[i] != 0) {
				printf("child, arr %d need to be processed, value is %d\n", i, arr[i]);
				arr[i] = 0;
			}

		sem_wait(&sem_fight);
	}

	munmap(arr, sizeof(int)*ARR_SIZE);
}

static void init_sem() {
	int ret = sem_init(&sem_fight, 0, 0);
	assert(!ret);
}

int main() {
	printf("parent speaking:pid=%d\n", getpid());
	struct event accept_event;
	int fd = build_socket();

	event_init();
	event_set(&accept_event, fd, EV_READ|EV_PERSIST, accept_callback, NULL);
	event_add(&accept_event, NULL);

	//create mmap
	arr = (int *)mmap(NULL, sizeof(int)*ARR_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	assert(arr != MAP_FAILED);

	//fork
	pid_t pid;
	if ((pid=fork()) == 0) {
		work_as_child();
		exit(0);
	}

	//semaphore
	init_sem();

	event_dispatch();

	close(fd);
	return 0;
}

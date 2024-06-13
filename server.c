#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CLIENTS 256
#define BUF_SIZE 4096
#define PORT 8080
#define EXIT_ERROR -1

typedef enum { STATE_NEW, STATE_CONNECTED, STATE_DISCONNECTED } state_e;

typedef struct {
  int fd;
  state_e state;
  char buffer[BUF_SIZE];
} client_t;

client_t clients[MAX_CLIENTS];

void init_clients() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    clients[i].fd = -1;
    clients[i].state = STATE_NEW;
    memset(&clients[i].buffer, '\0', BUF_SIZE);
  }
}

int find_free_slot() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].fd == -1) {
      return i;
    }
  }

  return -1;
}

int find_free_slot_by_fd(int fd) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].fd == fd) {
      return i;
    }
  }

  return -1;
}

int main() {
  int sfd, conn_fd, nfds, freeSlot;
  struct sockaddr_in serverInfo, client_addr;
  socklen_t client_len = sizeof(client_addr);

  struct pollfd fds[MAX_CLIENTS + 1];
  int opt = 1;

  init_clients();

  sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd == -1) {
    perror("socket");
    exit(EXIT_ERROR);
  }

  // to make the socket non waiting
  if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
    perror("sockopt");
    exit(EXIT_ERROR);
  }

  memset(&serverInfo, 0, sizeof(struct sockaddr_in));
  serverInfo.sin_addr.s_addr = INADDR_ANY;
  serverInfo.sin_family = AF_INET;
  serverInfo.sin_port = htons(PORT);

  if (bind(sfd, (struct sockaddr *)&serverInfo, sizeof(struct sockaddr_in)) ==
      -1) {
    perror("bind");
    exit(EXIT_ERROR);
  }

  if (listen(sfd, 10) == -1) {
    perror("listen");
    exit(EXIT_ERROR);
  }

  // fds, nfd
  memset(&fds, 0, sizeof(fds));
  fds[0].fd = sfd;
  fds[0].events = POLLIN;
  nfds = 1;

  printf("server listening on port: %d\n", PORT);

  while (1) {
    // update fd set to include clients that are connected
    int ii = 1; // offset by 1 for listenfd
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clients[i].fd != -1) {
        fds[ii].fd = clients[i].fd;
        fds[ii].events = POLLIN;
      }
    }

    // keep track of clients to serve
    int n_events = poll(fds, nfds, -1);
    if (n_events == -1) {
      perror("poll");
      exit(EXIT_ERROR);
    }

    // check if any of the returned events (revents) have data
    // to read, i.e. the POLLIN bit is set
    if (fds[0].revents & POLLIN) {
      if ((conn_fd = accept(sfd, (struct sockaddr *)&client_addr,
                            &client_len)) == -1) {
        perror("accept");
        continue;
      }

      freeSlot = find_free_slot();
      if (freeSlot == -1) {
        printf("Server full: closing new connection\n");
        close(conn_fd);
      } else {
        clients[freeSlot].fd = conn_fd;
        clients[freeSlot].state = STATE_CONNECTED;

        nfds++;
        printf("Slot %d assigned to clientfd %d", freeSlot, conn_fd);
      }

      n_events--;
    }

    // skipping socket fd fds[0]
    for (int i = 1; i <= nfds && n_events > 0; i++) {
      if (fds[i].revents & POLLIN) {
        n_events--;

        int fd = fds[i].fd;
        int slot = find_free_slot_by_fd(fd);

        ssize_t bytes_read =
            read(fd, clients[slot].buffer, sizeof(clients[slot].buffer));
        if (bytes_read <= 0) {
          // closed connection
          if (close(fd) == -1) {
            perror("close");
            if (slot == -1) {
              printf("Attempted to close non-existent fd\n");
            } else {
              clients[slot].fd = -1;
              clients[slot].state = STATE_DISCONNECTED;
              nfds--;

              printf("Client disconnected or error\n");
            }
          }
        } else {
          printf("Recieved from client %d: %s\n", fd, clients[slot].buffer);
        }
      }
    }
  }

  return 0;
}

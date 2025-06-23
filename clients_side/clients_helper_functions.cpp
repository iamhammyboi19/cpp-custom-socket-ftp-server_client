#include "clients_helper_functions.h"
#include <ios>
#include <iostream>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

struct kevent event;

// THIS IS USED TO SEND RAW BYTES WITH NON BLOCKING SOCKETS
void send_all(int kq, int socket_fd, const char *start, size_t buff_size) {
  size_t total_sent = 0;

  while (buff_size > total_sent) {
    // this makes it happen that it sends from the memory location it starts and moves forward (start + total_sent)
    // and it send the remaining data in the buffer (buff_size - total_sent)
    ssize_t bytes_sent = send(socket_fd, start + total_sent, buff_size - total_sent, 0);

    // there is no more bytes to send
    if (bytes_sent == 0) {
      std::cerr << "send_all unexpectedly sent 0... closing socket_fd" << std::endl;
      close(socket_fd);
      break;
    }

    // an error occurred
    if (bytes_sent == -1) {
      // that means the kernel buffer is full so now we need to check for when it is available to write again
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        //
        if (add_event_for_writable(&event, socket_fd, kq) == 0) {
          return;
        }

        // now wait to be notified and start restart the loop
        struct kevent event_trigger;
        if (check_event_for_writable(&event_trigger, socket_fd, kq) == 0) {
          return;
        }

        // restart the loop bytes_sent is already -1 so we can no longer do total_sent += sent
        continue;
      } else {
        std::cerr << "send_all header error for fd: (" << socket_fd << ") " << strerror(errno) << std::endl;
        close(socket_fd);
        return;
      }
    }

    total_sent += bytes_sent;
  }
  return;
}

// the close_socket here 0 means don't close the file socket yet
// uint8_t close_socket
void read_all(struct kevent &read_event, int real_socket_fd) {

  std::vector<char> recv_data(128);

  if (read_event.filter == EVFILT_READ) {

    int socket_fd = read_event.ident;

    // std::cout << "socket_fd: (" << socket_fd << ") got fired but the owner of the thread is: (" << real_socket_fd << ")" << std::endl;

    while (true) {
      // this is very simple tho just sending messages like "File successfully uploaded to the server!"
      ssize_t bytes_read = recv(socket_fd, recv_data.data(), recv_data.size(), 0);

      // std::cout << "bytes_read for socket fd: " << socket_fd << " is: " << bytes_read << std::endl;

      if (bytes_read == 0) {
        std::cout << "Client done sending to the server closing server socket... 0" << std::endl;
        close(socket_fd);
        break;
      }

      if (bytes_read > 0) {
        std::cout << "Recieved data from socket fd " << socket_fd << " :" << recv_data.data() << std::endl;
      }

      if (bytes_read == -1) {
        // the file is done reading close the socket
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          std::cerr << "read_all error for fd: (" << socket_fd << ") " << strerror(errno) << std::endl;
          close(socket_fd);
          break;
        }
      }
    }
  }

  return;
}

// ============================================================================================================================

// ============================================================================================================================
// NOTE this is adding it with just EV_ONESHOT ie as soon as the event is triggered you will have to register to check for it
bool add_event_for_writable(struct kevent *event, int client_s_fd, int kq) {
  EV_SET(event, client_s_fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, NULL, NULL);

  if (custom_kevent(kq, event, 1, static_cast<struct kevent *>(nullptr), 0, static_cast<const struct timespec *>(nullptr)) == -1) {
    std::cerr << "custom_kevent client_socket register for fd: (" << client_s_fd << ") " << strerror(errno) << std::endl;
    close(client_s_fd);
    return false;
  }

  return true;
}

bool check_event_for_writable(struct kevent *event, int client_s_fd, int kq) {
  if (custom_kevent(kq, nullptr, 0, event, 1, static_cast<const struct timespec *>(nullptr)) == -1) {
    std::cerr << "custom_kevent client_socket trigger for fd: (" << client_s_fd << ") " << strerror(errno) << std::endl;
    close(client_s_fd);
    return false;
  }

  return true;
}

// NOTE this is adding it with just EV_ONESHOT ie as soon as the event is triggered you will have to register to check for it

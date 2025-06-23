#include <cstdint>
#include <mutex>
#include <stddef.h>
#include <sys/event.h>

// send all
void send_all(int kq, int socket_fd, const char *start, size_t buff_size);

// recieve all
// uint8_t close_socket
void read_all(struct kevent &read_event, int socket_fd);

// custom kevent function
// template <typename... Args> int custom_kevent(Args &&...args);
// create a custom function for kevent()
// so this will have a single kevent() you can pass across threads
// inline std::mutex thread_locker;

template <typename... Args> int custom_kevent(Args &&...args) {
  //
  {
    // std::lock_guard<std::mutex> lock(thread_locker);
    return kevent(std::forward<Args>(args)...);
  }
}

// this is to add write event to a socket file descriptor to check if the kernel buffer is free to write to
bool add_event_for_writable(struct kevent *event, int client_s_fd, int kq);

// this is to wait for that event
bool check_event_for_writable(struct kevent *event, int client_s_fd, int kq);

// bool add_event_for_read_noti(struct kevent *event, int client_s_fd, int kq);

// bool check_event_for_read_noti(struct kevent *event, int client_s_fd, int kq);
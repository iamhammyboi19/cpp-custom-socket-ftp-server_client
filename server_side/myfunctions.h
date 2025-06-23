#ifndef MYFUNCTIONS_H
#define MYFUNCTIONS_H

#include "client_state.h"
#include <unordered_map>

// this function takes a file descriptor from epoll
// then it is wrapped inside a lambda function and pushed on to the threadpool
void read_data_from_client_socket_fd(int fd, std::unordered_map<int, ClientState> &all_clients_state);

void mark_client_for_cleanup_client(ClientState &cur_client, const std::string &msg);

#endif
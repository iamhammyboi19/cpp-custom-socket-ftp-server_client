// this is where I accept the binary protocol header from the client and decode it
// so I can monitor each clients by their socket_fd
// use the created std::ofstream to keep adding items to the file
// since each files is unique to the client, check if the file bytes sent is up to the file size
// if the user tries to close connection delete the file but if the file bytes sent is up to the file size save it

// the header binary protocol is like this:
// [4 bytes for the filename length], [this is the file name (this is based on how much the filename length is)],
// [8 bytes for the file size] so its like this [4bytes][unknown bytes based on the 4bytes value][8 bytes]

// so if a file is like this "thisvideo.mp4" and the size is "20mb"
// thisvideo.mp4 is 13 chars ie 13 bytes
// filename length will contain "0x0000000D on big endian" which means "thisvideo.mp4" has length of 13
// the name would be expected to be 13 bytes
// then finally we will have the file size 20,971,520 bytes which is "0x0000000001400000 on big endian"

// so if this is stored inside std::vector<uint8_t> then this will store the raw bytes as expected and we can just extract it
// since we know what size each of this is going to be and pick it at the offset, first 4bytes then 13bytes then 8bytes

#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdint.h>
#include <stdio.h>

#pragma once

struct ClientState {

  // this is the unique socket file descriptor for each users
  int client_socket_fd;

  // each clients will get a mutex this is needed for concurrency writes to ofstream
  std::mutex client_mutex;

  // this will make it handle a socket fd per thread
  // the thing is if a thread is currently handling a client that same client will have to wait
  // before it can be adding to the threadpool queue again
  std::atomic<bool> in_use{false};

  std::atomic<bool> mark_client_for_cleanup{false};

  // create an enum to check the stage which the client socket is for the specific client
  // update this as soon as you read the first sets of data sent from the client which is the header binary protocol
  enum Stage { ReadingFileHeader, ReadingFileStreams };

  Stage stage = ReadingFileHeader;

  // header part

  // file size
  uint64_t file_size{0};

  // bytes read
  uint64_t bytes_read{0};

  // file name
  // char *file_name = nullptr;
  std::string file_name;

  // the output file stream
  std::ofstream client_file_stream;

  ClientState(int fd) : client_socket_fd(fd) {};

  // move constructor
  ClientState(ClientState &&old_state) {
    client_socket_fd = old_state.client_socket_fd;
    file_size = old_state.file_size;
    bytes_read = old_state.bytes_read;
    file_name = old_state.file_name;
    stage = old_state.stage;
    client_file_stream = std::move(old_state.client_file_stream);
    // client_mutex = std::move(old_state.client_mutex);

    // old_state.file_name = nullptr;
  }

  // move assignment

  ClientState &operator=(ClientState &&old_state) {
    //
    if (this == &old_state) {
      return *this;
    }

    // delete this->file_name;

    this->client_socket_fd = old_state.client_socket_fd;
    this->file_size = old_state.file_size;
    this->bytes_read = old_state.bytes_read;
    this->stage = old_state.stage;
    this->file_name = old_state.file_name;
    this->client_file_stream = std::move(old_state.client_file_stream);

    // old_state.file_name = nullptr;

    return *this;
  }

  //

  //   destructor
  ~ClientState() {}

  // delete[] file_name;
};

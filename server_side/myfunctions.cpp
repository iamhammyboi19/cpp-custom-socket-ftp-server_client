#include "myfunctions.h"
#include "client_state.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

//

void mark_client_for_cleanup_client(ClientState &cur_client, const std::string &msg) {
  // std::cout << "marking client with fd: " << cur_client.client_socket_fd << " for cleanup" << std::endl;
  std::cerr << msg << cur_client.client_socket_fd << " errno: " << errno << " (" << strerror(errno) << std::endl;
  cur_client.mark_client_for_cleanup.store(true);
  return;
}

// this should be written in a way that shows we do not know how many bytes of data to expect from the client
// so this takes the data bytes by bytes till there is nothing left to read
// with file.txt

// THE LOGIC FOR THE BINARY HEADER PROTOCOL IS INSIDE THE client_state.h header file
// Each clients has mutex in their objects the purpose of this is to use it whenever things are getting critical
// ie while wrting to a client's file stream for example
// opening a file/closing a file
// so I am also making sure I am not delaying the mutex lock at all
void read_data_from_client_socket_fd(int fd, std::unordered_map<int, ClientState> &client_states) {
  //
  // find the current client socket file descriptor
  auto it = client_states.find(fd);

  // check if that client truly exists in the map
  if (it == client_states.end()) {
    std::cerr << "Socket with the file descriptor " << fd << " not found in client states" << std::endl;
    return;
  }

  // since this is key and value pair we want the value which is ->second, ->first is the key
  ClientState &cur_client = (it->second);

  // COPY THE HEADER DETAILS
  // check if the current stage is ReadingFileHeader then try and parse the binary header from what is received
  if (cur_client.stage == ClientState::ReadingFileHeader) {
    //
    std::vector<char> header_holder(1028);

    uint32_t file_name_len;

    // this is to make each file have a unique file name
    // i will prepend this to the original file name received from the clients
    std::chrono::time_point<std::chrono::system_clock> cur_time = std::chrono::system_clock::now();

    uint64_t append_to_file_name =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(cur_time.time_since_epoch()).count()) / fd;

    // try to read the whole header data without fixed bytes (realistically its just 3 things filename length, the filename and file size)
    // but I just want to do this way ie use 1028bytes to take the binary header received from the client

    // reading is really expensive
    ssize_t bytes_recv = recv(fd, header_holder.data(), header_holder.size(), 0);

    if (bytes_recv == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      } else {
        mark_client_for_cleanup_client(cur_client, "Unable to read header bad");
        return;
      }
    }

    // copy the filename length from data received using memcpy
    // this is 4bytes (uint32_t) memcpy will fill this up
    std::memcpy(&file_name_len, header_holder.data(), sizeof(file_name_len));

    // convert back from htonl to ntohl
    file_name_len = ntohl(file_name_len);

    // this is done to only send the response once ie if the thread A parse the header
    // it will update the value to true
    // while Thread B won't even get the chance to do this because while waiting for the lock the values has been updated
    bool send_header_response = false;

    // now lock the client and check if the client.ReadingFileHeader is active
    // this is because if a thread has done this there is no reason for another client to do it because the bytes has already been read and used
    {
      std::lock_guard<std::mutex> lock(cur_client.client_mutex);

      if (cur_client.ReadingFileHeader == ClientState::ReadingFileHeader) {

        // now copy the file name
        // the value of file_name_len is the length of the file name eg thefile.txt would be 11bytes
        // cur_client.file_name = new char[file_name_len];
        cur_client.file_name = std::string(header_holder.data() + sizeof(file_name_len), file_name_len);
        cur_client.file_name = std::to_string(append_to_file_name) + cur_client.file_name;

        // std::memcpy(&cur_client.file_name[0], header_holder.data() + sizeof(file_name_len), file_name_len);

        // copy the file size
        // the last 8 means copy 8 bytes from the position specified
        std::memcpy(&cur_client.file_size, header_holder.data() + sizeof(file_name_len) + file_name_len, 8);

        // convert from htonll to ntohll
        cur_client.file_size = ntohll(cur_client.file_size);

        // open the file to start writing to it
        cur_client.client_file_stream.open("./server-video/" + std::to_string(fd) + cur_client.file_name, std::ios::binary);

        if (!cur_client.client_file_stream.is_open()) {
          mark_client_for_cleanup_client(cur_client, "Error opening file stream for writing");
          return;
        }

        // now update the the stage for this specific client so the next read would be the file stream itself
        cur_client.stage = cur_client.ReadingFileStreams;

        send_header_response = true;
      }
    }

    if (send_header_response) {

      // the client is expecting and waiting for this message after successfully sending the header and the server reads it
      const char *response = "Header successfully read";
      send(cur_client.client_socket_fd, response, strlen(response), 0);
    }
    return;
  }

  // READ THE FILE STREAM SENT BY THE CLIENT TILL ITS bytes_read == file_size
  if (cur_client.stage == ClientState::ReadingFileStreams) {
    // read all the data from the socket

    const size_t buff_size = 16350;

    char temp_buff[buff_size];

    // use this boolean to handle closing up resources after server is done writing for client
    // ie total_bytes_read = file_size
    // bool final_file_read = false;

    // read from the socket till its drained THIS IS IMPORTANT because of EV_CLEAR which only notifies the server once
    while (true) {
      // the tricky thing about this part is that if THREAD A reads, then THREAD B reads from the server
      // it is possible THREAD B hold the lock before THREAD A and this would be bad because the write won't be in order
      // so this is why each clients have that "std::atomic<bool> in_use;" check to make sure one thread will handle just one client at a time
      // this is the best way I can think of handling this for now
      ssize_t bytes_read = recv(fd, temp_buff, buff_size, 0);

      // this means the client closed the connection so close the client socket
      if (bytes_read == 0) {
        mark_client_for_cleanup_client(cur_client, "Client closed connection: everything is done: ");
        break;
      }

      // this means the current data notified for read by kqueue is done
      if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // std::cout << "EAGAIN reached when bytes_read for fd: " << cur_client.client_socket_fd << " is: " << cur_client.bytes_read << std::endl;
          break;
        } else {
          mark_client_for_cleanup_client(cur_client, "not EAGAIN or EWOULDBLOCK for fd bad: ");
          break;
        }
      }

      {
        std::lock_guard<std::mutex> lock(cur_client.client_mutex);

        // // check before writing to the file IMPORTANT
        // if (!cur_client.client_file_stream.is_open()) {
        //   std::cerr << "[FATAL] Tried writing to closed stream for fd: " << cur_client.client_socket_fd << std::endl;
        //   break;
        // }

        // write to the file stream and flush immediately
        cur_client.client_file_stream.write(temp_buff, bytes_read);
        cur_client.client_file_stream.flush();

        // now increment the current bytes_read
        cur_client.bytes_read += bytes_read;
      }

      // break the loop early if the full file size has been read
      if (cur_client.bytes_read == cur_client.file_size) {
        // std::cout << "full read bytes_read == file_size for fd: " << cur_client.client_socket_fd << std::endl;
        // final_file_read = true;
        // break;

        const char *response = "Client file successfully uploaded to the server... closing socket";
        ssize_t bytes_sent = send(cur_client.client_socket_fd, response, strlen(response), 0);

        if (bytes_sent == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
          } else {
            mark_client_for_cleanup_client(cur_client, "socket from client could not send successful msg bad: ");
            break;
          }
        }

        if (bytes_sent == 0) {
          mark_client_for_cleanup_client(cur_client, "socket from client closed too early for fd: ");
          break;
        }

        if (bytes_sent > 0) {
          // std::cout << "sent completed successfully for fd: " << cur_client.client_socket_fd << " bytes_sent: (" << bytes_sent << ")" << std::endl;
        }

        // IMPORTANT!!! makes sure send() socket internal buffer is cleared before closing the file
        // without this the message might not reach the client and they just receive EOF as soon as its closed
        shutdown(cur_client.client_socket_fd, SHUT_WR);

        break;
      }
    }

    return;
  }

  return;
}

//

#include "clients_helper_functions.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

/*
what I plan to achieve here is to create specific numbers of clients specified by the char *argv[]
I want to use a single kqueue to handle these connections and spawn the clients connected in multiple threads
then I will use mutex to handle
*/

void connect_client_to_server(std::atomic<int> &total_uploads);
// int kq

// char *argv[]
int main(int argc, char *argv[]) {
  //
  int thread_count{0};
  if (argc > 1) {
    thread_count = std::atoi(argv[1]);
  }

  std::cout << "thread_count: " << std::atoi(argv[1]) << std::endl;

  if (thread_count == 0) {
    std::cout << "You cannot have less than 1 thread in this threadpool... Changing thread_count to 1" << std::endl;
    thread_count = 1;
  }

  std::atomic<int> total_uploads{0};

  // prevent the client from crashing if the server close has disconnected
  // this means ignore signal (sigpipe)
  signal(SIGPIPE, SIG_IGN);

  //   int kq = kqueue();

  // if (kq == -1) {
  //   perror("kqueue creation");
  //   return 123;
  // }

  std::vector<std::thread> all_threads;

  // starts
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

  for (int i = 0; i < thread_count; i++) {
    //
    all_threads.emplace_back([&total_uploads]() { connect_client_to_server(total_uploads); });
  }

  for (auto &thread : all_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  // ends
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

  // substracting two time_points returns a duration in nanoseconds
  // specifically std::chrono::duration<int64_t, std::ratio<1, 1,000,000,000>>
  // ie 1,000,000,000 nanoseconds = 1 sec
  std::chrono::nanoseconds time_taken = end - start;

  // casting duration from std::chrono::nanoseconds to std::chrono::seconds will round it up eg 2.83secs would be 2secs
  // to avoid that cast it to std::chrono::duration<double> defaults to 1 sec of whatever time measurement it is
  // std::ratio<1, 1000000000>
  std::chrono::duration<double> final_duration = time_taken;

  std::cout << total_uploads << " clients took " << final_duration.count() << "secs" << std::endl;

  return 0;
}

void connect_client_to_server(std::atomic<int> &total_uploads) {

  // set up a socket
  int client_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (client_socket == -1) {
    std::cerr << "client_socket creation failed for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
    return;
  }

  // get the current flag so the O_NONBLOCK won't override the previous set bits
  int get_current_flag = fcntl(client_socket, F_GETFL);

  if (get_current_flag == -1) {
    std::cerr << "F_GETFL client_socket for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
    close(client_socket);
    return;
  }

  // now set the socket to non blocking mode
  if (fcntl(client_socket, F_SETFL, get_current_flag | O_NONBLOCK) == -1) {
    std::cerr << "F_SETFL client_socket for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
    close(client_socket);
    return;
  }

  //   now set up a socket address and fill it up with the server details
  sockaddr_in server_address;

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(8050);
  inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr);

  // this is non blocking so it will return immediately
  if (connect(client_socket, (sockaddr *)&server_address, sizeof(server_address)) == -1) {
    //
    if (errno != EINPROGRESS) {
      std::cerr << "client socket cannot connect to server for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
      close(client_socket);
      return;
    }
  }

  int kq = kqueue();

  if (kq == -1) {
    perror("kqueue creation");
    return;
  }

  // when it returns register it with kevent and add it to kqueue
  // this is done so that we can check if the client socket is writable this means it has finish its connection
  // process to the server which will either fail or succeed
  // this is like saying let me know when the tcp handshake connection result is ready
  //  EV_ONESHOT is important because we want this to be removed or else it will constantly check for events
  // that we might not need after this and waste cpu resource (we will use this again later on)
  struct kevent event;

  // function from #include "clients_helper_functions.h"
  if (add_event_for_writable(&event, client_socket, kq) == 0) {
    return;
  }

  // after registering for the event wait for it to be triggered
  struct kevent triggered_event;

  // this will block the thread until it is triggered (ie the tcp connection result is ready)
  // function from #include "clients_helper_functions.h"
  if (check_event_for_writable(&triggered_event, client_socket, kq) == 0) {
    return;
  }

  // check if the client socket is now writable
  if (triggered_event.filter == EVFILT_WRITE) {

    // this is to check the result of the sockets tcp handshake
    int err;
    socklen_t len = sizeof(err);

    // now this takes the client_socket, checks the procotol level you want to query, and tell it to check if there is error or not
    // then store the status inside err
    if (getsockopt(client_socket, SOL_SOCKET, SO_ERROR, &err, &len) == -1) {
      std::cerr << "getsockopt client_socket for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
      close(client_socket);
      return;
    } else {
      // now check the value filled for err to check if an error occurred or not
      if (err != 0) {
        std::cerr << "SO_ERROR was " << strerror(err) << " (" << err << ") " << std::endl;
        std::cerr << "client_socket connection failed after tcp handshake for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
        close(client_socket);
        return;
      }

      // now read the file and send it to the server
      // there is no protocol here whatsoever for now to transmit data
      // I will manipulate the server side code to know how to handle each files sent

      // CREATE A BINARY HEADER PROTOCOL
      // [4 bytes for filename string length] [filename determined by the 2 bytes] [8 bytes to store file size]

      // ==========================================================================================================

      // the header binary protocol is like this:
      // [4 bytes for the filename length], [this is the file name (this is based on how much the filename length is)],
      // [8 bytes for the file size] so its like this [4bytes][unknown bytes based on the 4bytes value][8 bytes]

      // so if a file is like this "thisvideo.mp4" and the size is "20mb"
      // thisvideo.mp4 is 13 chars ie 13 bytes
      // filename length will contain "0x0000000D on big endian" which means "thisvideo.mp4" has length of 13
      // the name would be expected to be 13 bytes
      // then finally we will have the file size 20,971,520 bytes which is "0x0000000001400000 on big endian"

      // so if this is stored inside std::vector<char> then this will store the raw bytes as expected and we can just extract it
      // since we know what size each of this is going to be and pick it at the offset, first 4bytes then 13bytes then 8bytes

      // just wanna use c style char for fun here
      std::vector<char> custom_ftp_header;

      // ./sockets/clients_side/client-video/file.txt
      //
      //./client-video/file.txt
      const char *file_path = "~/sockets/clients_side/client-video/file.txt";

      // this will be used to update the last index of "/" in filepath so I can extract the file name
      int last_index_of_slash = -1;

      for (int i = 0; file_path[i] != '\0'; i++) {
        if (file_path[i] == '/') {
          last_index_of_slash = i;
        }
      }

      if (last_index_of_slash == -1) {
        std::cerr << "This is not a valid file path because of the absence of '/'" << std::endl;
        return;
      }

      // add plus 1 so it can move past the last '/'
      const char *file_name = file_path + last_index_of_slash + 1;

      // this is used to convert the filename length to big endian so a computer with little endian won't misinteprete the value
      // htonl works for 32 bits (4 bytes)
      uint32_t file_name_length = htonl(static_cast<uint32_t>(strlen(file_name)));

      //   this is not a text file so it must be opened in binary mode
      std::ifstream video_file(file_path, std::ios::binary);

      // if the file did not open for any client close the socket and end the function
      if (!video_file.is_open()) {
        std::cerr << "video_file !is_open for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
        close(client_socket);
        return;
      }

      // take the file stream internal pointer to the end of the file (the file is opened in binary mode so this is safe)
      video_file.seekg(0, std::ios::end);

      // htonll works for 64 bits (8 bytes)
      std::streampos pos = (video_file.tellg());

      // std::cout << "pos count: " << pos << std::endl;

      uint64_t file_size = htonll(static_cast<uint64_t>(pos));

      // std::cout << "file_size after pos: " << file_size << std::endl;

      // now take the internal pointer back to the beginning to the file knows where to start reading
      video_file.seekg(std::ios::beg);

      // copy all the raw binary inside custom_ftp_header
      // vector.insert expects the position you want to insert inside the vector in this case starting from the last position
      // the data you want to insert OR the start of the pointer and the end of the pointer
      // reinterpret_cast<char *> breaks the 4 byte to 1 byte each and we ask it to move to the 4th bytes
      custom_ftp_header.insert(custom_ftp_header.end(), reinterpret_cast<char *>(&file_name_length),
                               reinterpret_cast<char *>(&file_name_length) + sizeof(file_name_length));

      // now copy the file_name (this is already a pointer so its good)
      custom_ftp_header.insert(custom_ftp_header.end(), (file_name), (file_name) + strlen(file_name));

      // finally copy the file size which is 8 bytes
      custom_ftp_header.insert(custom_ftp_header.end(), reinterpret_cast<char *>(&file_size),
                               reinterpret_cast<char *>(&file_size) + sizeof(file_size));

      //  now send this header to the server
      send_all(kq, client_socket, custom_ftp_header.data(), custom_ftp_header.size());

      // now we have to wait till there is data to read just a response from the server saying it received the header
      // before sending the file stream
      struct kevent read_event;

      // register the read_event with kqueue so we can wait and get notified if there is something to read from the server
      EV_SET(&read_event, client_socket, EVFILT_READ, EV_ADD | EV_CLEAR | EV_ENABLE, 0, 0, NULL);

      if (custom_kevent(kq, &read_event, 1, static_cast<struct kevent *>(nullptr), 0, static_cast<const struct timespec *>(nullptr)) == -1) {
        std::cerr << "error registering read_event header response for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
        close(client_socket);
        return;
      }

      if (custom_kevent(kq, static_cast<struct kevent *>(nullptr), 0, &read_event, 1, static_cast<const struct timespec *>(nullptr)) == -1) {
        std::cerr << "error occurred waiting for header response for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
        close(client_socket);
        return;
      };

      // function from #include "clients_helper_functions.h"
      // THIS WILL REALLY HELP TO MAKE SURE THE HEADER HAS BEEN RECIEVED EVEN ITS DONE IN MULTIPLE THREAD
      // so the thread that reads it would likely send acknowledgement message back to the client
      read_all(read_event, client_socket);

      // 16kb file stream read
      const int BUFFER_SIZE = 16384;

      std::vector<char> video_bytes(BUFFER_SIZE);

      // uint64_t total_file = 0;

      while (true) {
        // read this and write it to the server socket
        video_file.read(video_bytes.data(), video_bytes.size());

        // check how much bytes was read from the file
        std::streamsize byte_read = video_file.gcount();

        // std::cout << "bytes_read: " << byte_read << std::endl;

        // if nothing is read break the loop
        if (byte_read == 0) break;

        // now for each data we send to the server socket I learn send() has an internal limit it can send
        // we have to get a way of sending this 16kb of each data read in chunks to send()
        // so we start from 0 for each iteration of this while(true) loop
        std::streamsize total_sent = 0;

        // it is important to do it this way ie we are trying to send the full 16kb(possibly or less if its last read)
        // bytes read in chunks to send()
        while (total_sent < byte_read) {
          // the calculation here is that video_bytes.data() is filled with 16kb of data
          // now we want to know the exact point we are currently which is why we start the offset from total_sent
          // now to know size of data we need to send we need have to check the remaining bytes (ie current_read - sent to server)
          // so sent means the exact amount of data send() was about to "SEND" to the server
          ssize_t sent = send(client_socket, video_bytes.data() + total_sent, byte_read - total_sent, 0);

          // std::cout << "sent: " << sent << std::endl;

          if (sent == 0) {
            std::cerr << "something went wrong with client socket from server side for fd: (" << client_socket << ") " << std::endl;
            close(client_socket);
            video_file.close();
            return;
          }

          //   check if an error occurred
          if (sent == -1) {
            // now check if this error is about the kernel buffer being full
            // this would happen if the server is busy ie the threadpool is full after it accepted the connection
            // and cannot recieve anymore data to write to its internal kernel buffer
            // send() writes to a kernal buffer before the os network forwards it to the server (which might not be able to recieve it right now)

            if (errno == EWOULDBLOCK || errno == EAGAIN) {
              // now tell the client socket to register the socket to kqueue and let you know whenever the buffer is free
              // function from #include "clients_helper_functions.h"
              if (add_event_for_writable(&event, client_socket, kq) == 0) {
                return;
              }
              // now wait to be notified and start restart the loop
              struct kevent event_trigger;
              // function from #include "clients_helper_functions.h"
              if (check_event_for_writable(&event_trigger, client_socket, kq) == 0) {
                return;
              }

              // restart the loop sent is already -1 so we can no longer do total_sent += sent
              continue;
            } else {
              std::cerr << "client_socket sending file stream to the server failed for fd: (" << client_socket << ") " << strerror(errno)
                        << std::endl;
              close(client_socket);
              video_file.close();
              return;
            }
          }

          total_sent += sent;
          // total_file += static_cast<uint64_t>(sent);
        }

        if (video_file.peek() == EOF) {
          break;
        }
      }

      video_file.close();

      // std::cout << "file size: " << pos << std::endl;
      // std::cout << "total_file: " << total_file << std::endl;

      // now that the client has sent the file to the server now check and wait for the server response and close the socket

      if (custom_kevent(kq, static_cast<struct kevent *>(nullptr), 0, &read_event, 1, static_cast<const struct timespec *>(nullptr)) == -1) {
        std::cerr << "error occurred waiting for successful upload response for fd: (" << client_socket << ") " << strerror(errno) << std::endl;
        close(client_socket);
        return;
      };

      // function from #include "clients_helper_functions.h"
      read_all(read_event, client_socket);

      // std::cout << "read_all done: " << client_socket << std::endl;

      close(client_socket);

      total_uploads++;

      // std::cout << "closed!" << std::endl;

      return;
    }
  }
}

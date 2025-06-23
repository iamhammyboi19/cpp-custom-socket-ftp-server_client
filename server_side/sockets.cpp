/* I struggled with understanding sockets at first but the deeper I dive into it
the more I understand why a lot of the inner mechanisms was abstracted from us
and why we get to use http websocket etc as already built libaries.

What made me have great interest in knowing what socket truly is behind http
websocket is because I wonder how much users can a webserver handle on a single
computer and why. It all feels like waste of time but at least I always wondered
how things work after learning deep nodejs concepts and how it relates deeply
with C/C++ libuv

What I will do here is try to explain things as much as I can and this socket is
for Mac OS using kqueue and kevents to monitor the file descriptor. I will also
edit it for Linux epoll later

fd is FILE DESCRIPTOR
*/
#include "client_state.h"
#include "myfunctions.h" // this is where read_data_from_client_socket_fd() is
#include "threadpool.h"  // this is my threadpool
#include <arpa/inet.h>   // gives access to inet_ptons
#include <fcntl.h>       // this is where the fcntl function comes from it is used to set/get files flag and permissions
#include <iostream>
#include <netinet/in.h> // this gives access to socket address structs eg sockaddr_in (majorly things you need for PORT number and ip address)
#include <signal.h>
#include <sys/event.h>  // this is where we get kqueue and kevent from
#include <sys/socket.h> // this contains everything related to socket
#include <unistd.h>

void safely_remove_from_kqueue(int kq, int fd);

int main() {

  // prevent the server from crashing if the client has disconnected and you're trying to send to that client
  // this means ignore signal (sigpipe)
  signal(SIGPIPE, SIG_IGN);

  //   this function returns a file descriptor indicating an opened socket
  //   my definition of socket will be a pipe that lets two processes communicate with each other
  //   in a real sense socket is not a file but it is treated as a file because we can write to its buffer and read from it
  //   socket is just a part of your RAM memory that you read from and write to so the file descriptor keeps details about that specific section
  //   so this int returned is an array index for a socket table managed by the kernel
  //   each socket table current index contains information about the socket eg buffers(RAM) it has access to, the state etc
  //   AF_INET means it is IPv4, SOCK_STREAM  means it is a tcp connection, 0 option means use protocol that matches tcp automatically
  int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  //  if an error occurred it will return -1 (a socket fd will always be the next available higher index)
  //  0, 1, and 2 are default for stderr stdin and stdout
  if (server_socket_fd <= -1) {
    // perror is used to print an error immediately after a system call fails
    perror("server_socket_fd creation");
    return 123;
  }

  // this will get all the current flags in a fd eg read, write permissions
  int current_flags = fcntl(server_socket_fd, F_GETFL);

  if (current_flags == -1) {
    perror("fcntl F_GETFL server_fd");
    close(server_socket_fd);
    return 123;
  }

  // now set the socket fd to NON-BLOCKING MODE this happens whenever you try to accept connections, read, write to the file
  // NON-BLOCKING MODE means the socket fd will return immediately rather than block the thread if there is no current action happening
  // current_flags | O_NONBLOCK is an OR wise bit thing ie if current flag is 00000010 NONBLOCKING might be 00000001
  // so this will return 00000011 meaning each bit already sets some flag in the file already
  // so if I did not do current_flags | O_NONBLOCK but use only O_NONBLOCK
  // it will unset all the previous file flags and set only the O_NONBLOCK resulting in something like 00000001 instead of 00000011
  if (fcntl(server_socket_fd, F_SETFL, current_flags | O_NONBLOCK) == -1) {
    perror("fcntl F_SETFL server_fd");
    close(server_socket_fd);
    return 123;
  }

  // ->  this is where I won't lie I got tired of socket in C/C++ at first because why do I have to go through all these stress
  // sockaddr_in is for IPv4 address it holds things like ip address, port number and the family which is ipv4
  // but all these details are later type casted to struct sock_addr which has just two properties {family, char}
  struct sockaddr_in address;

  // htons means host byte to network byte order
  // on Big Endian computers 8050 in hexadecimal is 0x1F72
  // meaning the large numbers gets stored first (this is the default one the network byte uses)
  // on Little Endian computers the smaller number comes first
  // meaning it will save this 8050 hexadecimal as 0x721F
  address.sin_port = htons(8050);

  // family is still IPv4
  address.sin_family = AF_INET;

  // this converts the ip address to binary which is what the general sock_addr struct char expects
  // sock_addr is generic ie all ip procotols will always end up as a sock_addr while binding the socket to the ip address and port number
  // it encodes it with 4 bytes since it is ipv4 127 is 0x7F, 0 is 0x0, 0 is 0x0, and 1 is 0x1
  // the char in sock_addr converts this hexdecimal to letters using ascii table taking the port number first then the ip address
  // 8050 is "P2" on ascii table while 127,0,0,1 is "DEL key", "NUL", "NUL", "SOH" on the ascii. the "P2" makes more sense here I guess
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

  // struct sockaddr looks like this
  /*
  struct sockaddr {
  __uint8_t       sa_len;
  sa_family_t     sa_family;
  char            sa_data[14];
  };
  // so inside char sa_data[14] is where we store the port and the ip address
  which looks like this [0x1F, 0x72, 0x7F, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
  */

  // now we have to bind the socket to the port and ip address
  if (bind(server_socket_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
    perror("bind server_socket_fd");
    close(server_socket_fd);
    return 123;
  }

  // start listening for clients that tries to connect
  // I made the mistake of not listening at first since I tot kqueue will handle listening automatically as an event I/O thing
  // but then I realized the code was not returning any num_of_events for me while kevent() waits and block
  if (listen(server_socket_fd, SOMAXCONN) == -1) {
    perror("listen");
    close(server_socket_fd);
    return 123;
  }

  // there is a function called kevent and a struct called kevent
  // the kevent struct is a data structure that accepts a fd and we tell it what action of event we want to be notified of
  // the kevent function takes the data from kevent struct and extract it for monitoring by adding it to kqueue
  // THE GOAL of this is just to register multiple fd for the same or different event notifications depending on what we need

  // now we have to register our server socket fd with kevent after creating kqueue
  // kqueue is a fd given to us by the kernel what it does it continuously check/monitor fd we register on kevent
  // kqueue job is to monitor socket_fd, kevent is to tell kqueue the type of actions we want to get notified for
  // eg socket_fd is registered then we tell it we want to be notified if an event happens with this socket maybe someone try to connect
  // then we will read the request/message from the new client trying to connect to accept it

  int kq = kqueue();

  if (kq == -1) {
    perror("create kqueue");
    close(server_socket_fd);
    return 123;
  }

  // now set the server socket_fd for listening event
  // normally an event is just what is called "event" notification that you need to happen on a socket fd
  // so the thing is instead of manually filling it out yourself just give it to EV_SET macro and it will get the job done for you
  // this makes it easy for you to reuse the same struct kevent resource rather than creating new one everytime
  struct kevent event;

  // so this is like saying on this kevent "&event", take this resource identify ie "socket_fd"
  // notify me whenever there is something available for me to read from it "EVFILT_READ" event
  // and help me add and enable this event to kqueue "EV_ADD" "EV_ENABLE"
  // 0, 0, NULL (special filter flag (not needed), optional data (not also needed), and this is a pointer maybe to a user metadata you wanna use later
  // that can be added to the specific socket_fd (still not needed))
  EV_SET(&event, server_socket_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);

  // now this is the part where we now add the event to kqueue using kevent function
  // NOTE struct kevent is different from kevent function
  // struct kevent let us know the purpose of that "socket_fd" what we wanna do with it
  // function kevent helps add this event "socket_fd" to kqueue

  // now let me analyse kevent function params
  // kq means add &event ie "socket_fd" to kqueue for events notification
  // the 3rd param means you're changing (adding or removing) 1 event from the kqueue in this case adding
  // NULL, 0, NULL means (array of struct kevents, number of array size, and number of milliseconds to timeout while waiting for an event)
  // these are not needed here which is why I use NULL, 0, NULL
  kevent(kq, &event, 1, NULL, 0, NULL);

  // now this is where we check and handle events that occurred

  // try to get the socket details and print it to the console
  // to show something like this socket ip listening on port
  // now this will fill up the server_socket_details
  struct sockaddr_in server_socket_details;
  socklen_t len = sizeof(server_socket_details);
  if (getsockname(server_socket_fd, (sockaddr *)&server_socket_details, &len) == -1) {
    perror("get server sockname");
    return 123;
  }

  // retrieve it here and print it to the console

  // this will store the ip address as a string
  char ip_address_str[INET_ADDRSTRLEN];
  // this will help us fill up ip_address_str with the ip address
  inet_ntop(AF_INET, &server_socket_details.sin_addr, ip_address_str, sizeof(ip_address_str));
  int port = ntohs(server_socket_details.sin_port);

  std::cout << ip_address_str << " listening on port: " << port << std::endl;

  // initialize the threadpool before accepting clients
  ThreadPool thread_pool(8);

  // this is where we store each client struct object
  std::unordered_map<int, ClientState> all_clients_state;

  while (1) {
    // this is an array of struct kevent (10 items) that we want to take from kqueue whenever we have an event
    // here it means the highest of events it can take if just 1 event happens it returns it to us
    // if 20 events happens we take the first 10 events and while the first loop is done we take the rest
    struct kevent triggered_events[10];

    // std::cout << "reached here?" << std::endl;

    // struct timespec timeout;
    // timeout.tv_sec = 5; // wait max 5 seconds
    // timeout.tv_nsec = 0;

    // spot the difference? we are not adding or removing any changelist to kqueue which is why NULL, 0 comes after kq
    // instead we are extracting events that occurred in kq and it at most 10 events if there are more than 10 the next while
    // loop iteration will extract it for us
    // the reason it is done this way is because a function turns an array to a pointer to its first element
    // so now we do not know how many items are in the array which is why we pass the array size argument
    // NOTE the timeout being NULL means this part will block (it is needed so we won't be using CPU time unnecessarily inside the while loop)
    int num_of_events = kevent(kq, NULL, 0, triggered_events, 10, NULL);

    // cleanup clients here in the main thread
    for (auto it = all_clients_state.begin(); it != all_clients_state.end();) {

      // checks if the client is marked for clean up in the threadpool and if its not currently in use by any thread
      if (it->second.mark_client_for_cleanup.load(std::memory_order_acquire) && !it->second.in_use.load(std::memory_order_acquire)) {
        // close the file

        // std::cout << "FOR LOOPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP" << std::endl;
        if (it->second.client_file_stream.is_open()) {
          it->second.client_file_stream.close();
        }

        // close its socket fd
        safely_remove_from_kqueue(kq, it->second.client_socket_fd);

        // this erases by iterator and it returns the next iterator which is what will be used in the next loop iteration
        it = all_clients_state.erase(it);
      } else {
        // if current one is not true with the condition move to the next client and check
        ++it;
      }
    }

    // std::cout << "num_of_events: " << num_of_events << std::endl;

    // now loop through these sockets returned
    for (int i = 0; i < num_of_events; i++) {
      // this one is checking if the triggered_event is the server socket ie a new user wants to connect to the server
      // then what we do is accept this connection and add the client socket user for EVFILT_READ too
      // NOTE: warning: comparison of integers of different signs: 'uintptr_t'
      // GOT that compiler warning so I did this
      if (server_socket_fd >= 0 && triggered_events[i].ident == static_cast<uintptr_t>(server_socket_fd)) {
        int new_client_socket_fd = accept(server_socket_fd, NULL, NULL);

        // if an error occurred with the new client socket fd handle it gracefully
        if (new_client_socket_fd == -1) {
          perror("accept");
          continue;
        }

        // if fcntl cannot get flag close the socket fd and handle the error gracefully
        int client_flag = fcntl(new_client_socket_fd, F_GETFL);
        if (client_flag == -1) {
          perror("fcntl F_GETFL");
          close(new_client_socket_fd);
          continue;
        }

        // if fcntl cannot set flag close the socket fd and handle the error gracefully
        if (fcntl(new_client_socket_fd, F_SETFL, client_flag | O_NONBLOCK) == -1) {
          perror("fcntl F_SETFL O_NONBLOCK");
          close(new_client_socket_fd);
          continue;
        }

        // now add this new client connection to the kernel kqueue
        EV_SET(&event, new_client_socket_fd, EVFILT_READ | EV_CLEAR, EV_ADD | EV_ENABLE, 0, 0, NULL);
        kevent(kq, &event, 1, NULL, 0, NULL);
        std::cout << new_client_socket_fd << " socket fd tcp handshake completed" << std::endl;
        std::cout << "adding new socket to map..." << std::endl;

        // send(new_client_socket_fd, "You can start sending the file header", 36, 0);

        // create a new client state and store it in the unordered_map
        // ClientState new_client_state;
        // new_client_state.client_socket_fd = new_client_socket_fd;

        // this is the best way to handle this
        // std::ofstream can't be copied and I do not want to stress writing move constructors/assignments
        // also its just better this way to not waste unnecessary resources since its constructed right inside the unordered_map
        all_clients_state.emplace(new_client_socket_fd, ClientState(new_client_socket_fd));
      }
      // this means this is a client that has already connected and is trying to send message to the server
      else {
        //
        int fd = triggered_events[i].ident;

        // read_data_from_client_socket_fd(fd, all_clients_state);

        // find the current client socket file descriptor
        auto it = all_clients_state.find(fd);

        // check if that client truly exists in the map
        if (it == all_clients_state.end()) {
          // std::cerr << "Socket with the file descriptor " << fd << " not found in client states" << std::endl;
          safely_remove_from_kqueue(kq, fd);
          continue;
        }

        // since this is key and value pair we want the value which is ->second, ->first is the key
        ClientState &cur_client = (it->second);

        // if in_use returns false it checks is condition (!true)? Yeah (false satisfies !true)... it updates the value to true
        // if in_use returns true it check is condition (!true)? No (true does not satisfy !true)... it still updates the value to true
        if (!cur_client.in_use.exchange(true)) {
          int cur_fd = cur_client.client_socket_fd;
          thread_pool.enqueue([cur_fd, &all_clients_state]() {
            read_data_from_client_socket_fd(cur_fd, all_clients_state);
            // when a thread is done with reading current client it returns its in_use to false
            // this makes sure only one thread handle a client at a time

            // find the current client socket file descriptor
            // i am finding the client again because at first I was cleaning up the client in the threadpool (changed this logic)
            // so after debugging I found out I erase clients in read_data_from_client_socket_fd
            // making them invalid for changing in_use to false which is what I am fixing here for segfault
            auto it = all_clients_state.find(cur_fd);
            if (it != all_clients_state.end()) {
              it->second.in_use.store(false);
            }
          });
        }
      }
    }
  }

  //
  return 0;
}

void safely_remove_from_kqueue(int kq, int fd) {
  struct kevent remove_event;
  EV_SET(&remove_event, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  kevent(kq, &remove_event, 1, NULL, 0, NULL);
  close(fd);
}

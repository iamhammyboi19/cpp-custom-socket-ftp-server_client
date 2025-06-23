## Custom Socket File Transfer Protocol (Low Level System TCP/IP)

This project implements a custom file transfer protocol in C++ using low-level system features like `kqueue/kevent`, multithreaded event handling, and a binary header protocol.
It allows clients to send file metadata (name, size, extension) followed by the actual file stream to the server efficiently.

#### 🔧 Features

- Event-driven I/O using kqueue for non-blocking socket communication.
- Thread pool to manage multiple concurrent clients efficiently.
- Binary header protocol to transmit file metadata before the actual data stream.
- File streaming and buffering with performance tracking.
- Per-client locking via `std::atomic<bool> and std::mutex` to prevent race conditions during parallel processing.

#### 📈 Performance Metrics

Performance is measured by sending a `469.2MB` file from clients using multithreaded `kqueue` clients to the server.
The benchmarks simulate 1, 10, and 50 clients sending files in parallel.
This was compiled and ran with Macbook 2020 m1 with 8 cpu cores

Metrics include:

- Throughput (MB/s or GB/s)
- Average Time taken for transfer
- CPU/RAM usage on both server (s) and client (c) sides

| client | file_size | thput(avg) | server_threadpool | cpu(s) | ram(s) | cpu(c) | ram(c) | avg_time | min_time | max_time |
| ------ | --------- | ---------- | ----------------- | ------ | ------ | ------ | ------ | -------- | -------- | -------- |
| 1      | 469.2MB   | 664MB/s    | 0                 | 85%    | 1.1MB  | 35%    | 1.1MB  | 0.7062s  | 0.6604s  | 0.8689s  |
| 10     | 4.69GB    | 896MB/s    | 0                 | 98%    | 1.8MB  | 50%    | 1.7MB  | 5.1114s  | 4.2384s  | 5.7717s  |
| 50     | 23.46GB   | 859MB/s    | 0                 | 98%    | 1.8MB  | 51%    | 2.7MB  | 27.305s  | 24.788s  | 30.330s  |
| 1      | 469.2MB   | 653MB/s    | 8                 | 197%   | 1.5MB  | --     | 1.0MB  | 0.7177s  | 0.6723s  | 0.8212s  |
| 10     | 4.69GB    | 1.34GB/s   | 8                 | 528%   | 2.0MB  | 73.8%  | 1.7MB  | 3.4927s  | 3.2031s  | 4.0791s  |
| 50     | 23.46GB   | 1.55GB/s   | 8                 | 527.4% | 2.9MB  | 145.8% | 3.5MB  | 15.042s  | 13.942s  | 16.999s  |

#### ⚠️ Known Limitations & Ongoing Research:

1. Each client has its own `kqueue` instance.

   - Centralizing client FDs into one `kqueue` caused cross-thread fd handling issues.
   - Thread A socket fd might be the one to handle notification of Thread B socket fd causing wrong socket close and clean up.
   - Ongoing research: using a unified `kqueue` in the main thread and distributing work via a task queue.

2. Each client thread opens its own file.

   - This contributes to file descriptor exhaustion.
   - Planned solution: use `mmap()` to memory-map the file once and share across threads.

3. Server-side thread pool only allows one thread per client.
   - Prevents race conditions, but reduces parallelism when reading from one client.
   - This means if Thread A reads first from the socket and Thread B reads from it after Thread A.
   - Thread B might acquire the mutex lock before Thread A leading to race condition writes.
   - So I am currently making research to improve this because in the test.
   - Benchmark: 1-thread client + no thread pool is 0.011s faster than with a pool of 8.
   - However, with many clients, the 8-thread server is 1.4–1.8x faster.

#### 🧪 Future Improvements:

1. Build an `epoll-based` client on Linux and test inter-platform transfer (Linux → macOS).
2. Build a Linux-based server using `epoll`, with macOS as the client.
3. Integrate `sendfile()` to eliminate user-space buffering and speed up transfer (from file -> socket buffer).
4. Research and experiment with `io_uring` + `epoll` + `thread pool` to optimize kernel-level I/O.

#### 💬 Final:

This project is a hands-on exploration of building fast and scalable socket-based servers using low-level Unix/Linux primitives.
While early results are promising, there’s still room for optimization — especially in reducing FD usage and improving concurrency models across client and server.

#### 💻 Build & System Info:

This project was compiled (using makefile) and tested on a MacBook Pro (2020, M1 chip, 8-core CPU) using g++ with C++17 and system-level threading + kqueue APIs.

#### 🔗 Repository Structure

- `clients_side/`, `server_side/` cd into these folders and run make
- `clients_side/client-video/file_name.extension` (put any file in this folder `clients_side/client-video/`)
- `server_side/server-video` where server store the files
- in `./run_tests.cpp` put the absolute path `server_side/server-video` so it can delete the previous files before running next test

- run `clients_side` like this `clients_side/client_side 10` 10 is the clients (threads) size
- run `run_test` like this `./run_tests.cpp 10` 10 here is also how many threads you want each clients to spawn per test
- run_test runs 10 tests in total

- comment out this part in `server_side/sockets.cpp` if you want to run with 0 threadpool. 0 threadpool means just the main thread `line 295 - 327` and `line 191`. Finally uncomment `line 293`

` // read_data_from_client_socket_fd(fd, all_clients_state);`

`ThreadPool thread_pool(8);`

<pre> ```
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

``` </pre>

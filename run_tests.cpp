#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

int main(int argc, char *argv[]) {

  const int nums_of_run = 10;
  std::vector<double> time_taken_for_each_run;

  std::string num_of_clients = argv[1];

  std::string client_side = "./clients_side/client_side " + num_of_clients;

  for (int i = 0; i < nums_of_run; ++i) {
    // clear the folder before next program to avoid huge disk memory consumption

    std::filesystem::path folder("~/sockets/server_side/server-video");

    if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder)) {
      for (const auto &entry : std::filesystem::directory_iterator(folder)) {
        std::error_code ec;
        std::filesystem::remove_all(entry.path(), ec);
        if (ec) {
          std::cerr << "something went wrong removing " << entry.path() << "(" << ec.message() << ")" << std::endl;
        }
      }

    } else {
      std::cerr << "folder doesn't exist or is not a directory" << std::endl;
    }

    // now start the time for each program
    std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();
    int result = std::system(client_side.c_str());
    std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_taken = (end - start);

    if (result == 0) {
      time_taken_for_each_run.push_back((time_taken.count()));
    }
  }

  double sum = std::accumulate(time_taken_for_each_run.begin(), time_taken_for_each_run.end(), 0.0);
  double avg = sum / time_taken_for_each_run.size();

  auto [min_time, max_time] = std::minmax_element(time_taken_for_each_run.begin(), time_taken_for_each_run.end());

  std::cout << "for " << nums_of_run << " clients " << std::endl;
  std::cout << "avg time is: " << avg << std::endl;
  std::cout << "min time is: " << *min_time << std::endl;
  std::cout << "max time is: " << *max_time << std::endl;

  //
  return 0;
}
// static_cast<double>
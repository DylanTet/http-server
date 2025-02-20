#include <algorithm>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

std::string construct_http_response(std::string &body) {
  std::string response =
      "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ";
  response.append(std::to_string(body.size()));
  response.append("\r\n\r\n");
  response.append(body.data());

  return response;
}
std::string read_from_http_file(std::string_view file_path) {
  std::ifstream file(file_path);
  std::string file_content;
  std::getline(file, file_content, '\0');
  return file_content;
}
void handle_path(int client_fd, std::string &path,
                 const std::unordered_map<std::string, std::string> &headers,
                 std::string_view http_files_path) {
  if (path == "/") {
    std::string exists = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_fd, exists.c_str(), exists.length(), 0);
  }

  if (path.find("echo") != std::string::npos) {
    std::string echo_string = path.substr(6, std::string::npos);
    std::string res = construct_http_response(echo_string);
    send(client_fd, res.c_str(), res.length(), 0);
  }

  if (path.find("user-agent") != std::string::npos) {
    std::string agent = headers.find("User-Agent")->second;
    std::string res = construct_http_response(agent);
    send(client_fd, res.c_str(), res.length(), 0);
  }

  if (path.find("files") != std::string::npos) {
    std::string file_name = path.substr(path.find("files/"));
    if (std::filesystem::exists(http_files_path.data() + file_name)) {
      std::string http_file_content = read_from_http_file(http_files_path);
      std::string res = construct_http_response(http_file_content);
      send(client_fd, res.c_str(), res.length(), 0);
    } else {
      std::string no_exist = "HTTP/1.1 404 Not Found\r\n\r\n";
      send(client_fd, no_exist.c_str(), no_exist.length(), 0);
    }
  }

  else {
    std::string no_exist = "HTTP/1.1 404 Not Found\r\n\r\n";
    send(client_fd, no_exist.c_str(), no_exist.length(), 0);
  }
}

void handle_client_connection(int client_fd) {
  std::string total_request;

  // Fill up our string with the total bytes from the request
  while (true) {
    char buffer[1024];
    int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
    if (bytes_read <= 0) {
      break;
    }
    total_request.append(buffer, bytes_read);

    std::stringstream stream(total_request);
    std::string method, path, version;
    stream >> method >> path >> version;

    std::string line;
    std::unordered_map<std::string, std::string> headers;
    while (std::getline(stream, line)) {
      if (line.empty()) {
        break; // Empty line marks end of headers
      }

      // Find the colon separator
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        // Skip the colon and any leading spaces
        std::string value = line.substr(colon + 1);
        value = value.substr(value.find_first_not_of(' '));
        value.pop_back();
        headers[key] = value;
      }
    }
    handle_path(client_fd, path, headers);
  }
}

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::string dir_path;
  if (argv[1] == "--directory") {
    dir_path = argv[2];
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
      0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  std::vector<std::thread> client_threads;

  while (true) {

    std::cout << "Waiting for a client to connect...\n";
    int client = accept(server_fd, (struct sockaddr *)&client_addr,
                        (socklen_t *)&client_addr_len);

    std::cout << "Client connected\n";
    client_threads.emplace_back(handle_client_connection, client);
  }

  for (auto &thread : client_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  close(server_fd);

  return 0;
}

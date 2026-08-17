#include <iostream>
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

bool scan_port(const std::string& ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    
    return (result == 0);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <hostname>" << std::endl;
        return 1;
    }
    
    struct hostent* he = gethostbyname(argv[1]);
    if (!he) {
        std::cerr << "DNS resolution failed" << std::endl;
        return 1;
    }
    
    std::string ip = inet_ntoa(*(struct in_addr*)he->h_addr_list[0]);
    std::cout << "Scanning target: " << ip << std::endl;
    
    for (int port = 1; port <= 81; port++) {
        if (scan_port(ip, port)) {
            std::cout << "Port " << port << " is open" << std::endl;
        }
    }
    
    return 0;
}
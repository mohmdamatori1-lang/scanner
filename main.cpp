#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

using namespace std;

bool scan_port(const string& ip, int port, int timeout_ms = 1000) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "Socket creation failed: " << strerror(errno) << endl;
        return false;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(sock);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    if (result == 0) {
        close(sock);
        return true;
    }

    if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        close(sock);
        return false;
    }

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    result = select(sock + 1, nullptr, &writefds, nullptr, &tv);
    
    if (result < 0) {
        cerr << "Select error: " << strerror(errno) << endl;
        close(sock);
        return false;
    }
    
    if (result == 0) {
        // Timeout
        close(sock);
        return false;
    }

    int so_error = 1;
    socklen_t len = sizeof(so_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
        close(sock);
        return false;
    }

    close(sock);
    return so_error == 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        cout << "Usage: " << argv[0] << " <hostname> [start_port] [end_port]" << endl;
        cout << "Example: " << argv[0] << " scanme.nmap.org 1 100" << endl;
        return 1;
    }

    string hostname = argv[1];
    int start_port = (argc >= 3) ? atoi(argv[2]) : 1;
    int end_port = (argc >= 4) ? atoi(argv[3]) : 1024;
    int timeout_ms = 2000;  // 2 second timeout
    
    if (start_port < 1 || end_port > 65535 || start_port > end_port) {
        cerr << "Invalid port range" << endl;
        return 1;
    }

    struct hostent* he = gethostbyname(hostname.c_str());
    if (!he) {
        cerr << "DNS resolution failed for " << hostname << endl;
        return 1;
    }

    string ip = inet_ntoa(*(struct in_addr*)he->h_addr_list[0]);
    cout << "Scanning target: " << hostname << " (" << ip << ")" << endl;
    cout << "Port range: " << start_port << "-" << end_port << endl;
    cout << "Timeout: " << timeout_ms << "ms" << endl;
    cout << "----------------------------------------" << endl;

    vector<int> ports;
    for (int port = start_port; port <= end_port; ++port) {
        ports.push_back(port);
    }

    cout << "Testing connectivity..." << endl;
    if (scan_port(ip, 80, 2000)) {
        cout << "Port 80 is open (good, host is reachable)" << endl;
    } else {
        cout << "Port 80 appears closed/filtered (host may still be reachable)" << endl;
    }
    cout << "----------------------------------------" << endl;

    unsigned int num_threads = thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 8;
    if (num_threads > ports.size()) num_threads = ports.size();
    if (num_threads > 50) num_threads = 50;  
    cout << "Using " << num_threads << " threads" << endl;
    cout << "Scanning..." << endl;

    atomic<size_t> next_index{0};
    atomic<int> scanned_count{0};
    vector<int> open_ports;
    mutex open_ports_mutex;

    vector<thread> workers;
    for (unsigned int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                size_t i = next_index.fetch_add(1);
                if (i >= ports.size()) break;

                int port = ports[i];
                if (scan_port(ip, port, timeout_ms)) {
                    lock_guard<mutex> lock(open_ports_mutex);
                    open_ports.push_back(port);
                    cout << "Port " << port << " is open" << endl;
                }
                
                int count = ++scanned_count;
                if (count % 10 == 0 || count == ports.size()) {
                    cout << "\rProgress: " << count << "/" << ports.size() 
                         << " (" << (count * 100 / ports.size()) << "%)" << flush;
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    cout << endl << "----------------------------------------" << endl;
    sort(open_ports.begin(), open_ports.end());

    if (open_ports.empty()) {
        cout << "No open ports found" << endl;
    } else {
        cout << "Open ports: ";
        for (size_t i = 0; i < open_ports.size(); ++i) {
            if (i > 0) cout << ", ";
            cout << open_ports[i];
        }
        cout << endl;
    }

    return 0;
}

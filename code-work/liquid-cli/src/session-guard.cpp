// Native, idle-blocking watchdog. No Python interpreter, terminal window or timer.
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool validUnit(const std::string &unit) {
    if (unit.size() != 49 || unit.substr(0, 11) != "liquid-cli-" || unit.substr(43) != ".scope") return false;
    return unit.find_first_not_of("0123456789abcdef", 11) == 43;
}
static std::string unhex(const char *text) {
    const std::string s(text);
    if (s.size() % 2 || s.size() > 32768) return {};
    std::string result;
    auto nibble = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (size_t i = 0; i < s.size(); i += 2) {
        int a = nibble(s[i]), b = nibble(s[i+1]);
        if (a < 0 || b < 0 || !(a || b)) return {};
        result.push_back(char(a * 16 + b));
    }
    return result;
}
static int stopUnit(const char *unit) {
    const pid_t child = fork();
    if (child == 0) {
        const int null = open("/dev/null", O_RDWR);
        for (int i = 0; i < 3; ++i) dup2(null, i);
        execl("/usr/bin/systemctl", "systemctl", "--user", "stop", unit, static_cast<char *>(nullptr));
        _exit(127);
    }
    if (child < 0) return 1;
    int status = 0;
    for (int i = 0; i < 80; ++i) {
        if (waitpid(child, &status, WNOHANG) == child) return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        usleep(100000);
    }
    kill(child, SIGKILL); waitpid(child, &status, 0); return 1;
}
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    if (std::strcmp(argv[1], "--run") == 0) {
        const std::string unit(argv[2]), cwd = unhex(argv[3]), command = unhex(argv[4]);
        if (!validUnit(unit) || cwd.empty() || command.empty()) return 2;
        if (chdir(cwd.c_str()) != 0) { perror("无法进入工作目录"); return 1; }
        execl("/usr/bin/systemd-run", "systemd-run", "--user", "--scope", "--quiet", "--collect",
              "--property=TimeoutStopSec=2s", "--property=KillMode=control-group", "--unit", argv[2],
              "--", "/bin/bash", "-lc", command.c_str(), static_cast<char *>(nullptr));
        perror("无法启动终端会话"); return 1;
    }
    if (std::strcmp(argv[1], "--guard") != 0 || !validUnit(argv[4])) return 2;
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 1;
    struct sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (std::strlen(argv[2]) >= sizeof(address.sun_path)) return 2;
    std::strcpy(address.sun_path, argv[2]);
    if (connect(fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) != 0) return 1;
    struct ucred peer{}; socklen_t size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 || peer.uid != getuid() || peer.pid != std::atoi(argv[3])) return 1;
    struct pollfd wait{fd, POLLIN | POLLHUP, 0};
    for (;;) {
        if (poll(&wait, 1, -1) < 0) continue;
        char data[8];
        if (read(fd, data, sizeof(data)) <= 0) break;
    }
    close(fd);
    // A click immediately followed by closing can precede scope registration.
    for (int i = 0; i < 20; ++i) {
        if (stopUnit(argv[4]) == 0) return 0;
        usleep(100000);
    }
    return 1;
}

#include "http_socket.hpp"
#include <netinet/in.h>
#include <sys/socket.h>

curl_socket_t itu_open_loopback(void*, curlsocktype, curl_sockaddr* address) noexcept {
    bool allowed = false;
    if (address->family == AF_INET) {
        const auto* value = reinterpret_cast<const sockaddr_in*>(&address->addr);
        allowed = (ntohl(value->sin_addr.s_addr) >> 24) == 127;
    } else if (address->family == AF_INET6) {
        const auto* value = reinterpret_cast<const sockaddr_in6*>(&address->addr);
        allowed = IN6_IS_ADDR_LOOPBACK(&value->sin6_addr);
    }
    return allowed ? socket(address->family, address->socktype, address->protocol) : CURL_SOCKET_BAD;
}

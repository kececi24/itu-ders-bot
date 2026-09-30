#pragma once
#include <curl/curl.h>

// Compiled only into the offline fixture library, never production executables.
curl_socket_t itu_open_loopback(void*, curlsocktype, curl_sockaddr*) noexcept;

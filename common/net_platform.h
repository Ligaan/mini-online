#pragma once
#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;

inline int net_init()
{
    WSADATA d;
    return WSAStartup(MAKEWORD(2,2), &d);
}

inline void net_cleanup()
{
    WSACleanup();
}

inline void net_close(socket_t s)
{
    closesocket(s);
}

inline int  net_poll(pollfd* f, unsigned long n, int ms)
{
    return WSAPoll(f, n, ms);
}

inline bool net_invalid(socket_t s)
{
    return s == INVALID_SOCKET;
}

inline void net_perror(const char* w)
{
    std::fprintf(stderr, "%s: winsock error %d\n", w, WSAGetLastError());
}

#ifndef SIO_UPD_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

inline void net_disable_connreset(socket_t s)
{
    BOOL off = FALSE;
    DWORD n = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &n, nullptr, nullptr);
}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
using socket_t = int;

inline int  net_init()
{
    return 0;
}

inline void net_cleanup() {}
inline void net_close(socket_t s) { close(s); }
inline int  net_poll(pollfd* f, unsigned long n, int ms) { return poll(f, n, ms); }
inline bool net_invalid(socket_t s) { return s < 0; }
inline void net_perror(const char* w) { std::perror(w); }
inline void net_disable_connreset(socket_t) {}

#endif
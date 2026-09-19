#include "ws2_32.h"
#include <iphlpapi.h>

START_TEST(MulticastInterface)
{
    WSADATA data;
    SOCKET socket;
    ULONG value, readback;
    int result, length;
    struct sockaddr_in address = {0};
    result = WSAStartup(MAKEWORD(2,2), &data);
    ok(result == 0, "startup: %d\n", result);
    if (result) return;
    socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, 0);
    ok(socket != INVALID_SOCKET, "socket: %d\n", WSAGetLastError());
    if (socket == INVALID_SOCKET) goto done;
    length = sizeof(readback);
    readback = 0xdeadbeef;
    result = getsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&readback, &length);
    ok(!result && !readback && length == sizeof(readback), "default: %d/%lx/%d\n", result, readback, length);
    value = inet_addr("127.0.0.1");
    length = sizeof(readback);
    result = getsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&readback, &length);
    ok(!result && readback == 1, "multicast TTL default: %d/%lu\n", result, readback);
    result = setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&value, sizeof(value));
    ok(!result, "set before bind: %d\n", WSAGetLastError());
    address.sin_family = AF_INET;
    result = bind(socket, (struct sockaddr *)&address, sizeof(address));
    ok(!result, "bind: %d\n", WSAGetLastError());
    length = sizeof(readback);
    result = getsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&readback, &length);
    ok(!result && readback == value, "after bind: %d/%lx\n", result, readback);
    value = inet_addr("192.0.2.254");
    result = setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&value, sizeof(value));
    ok(result == SOCKET_ERROR, "invalid interface accepted\n");
    length = sizeof(readback);
    result = getsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&readback, &length);
    ok(!result && readback == inet_addr("127.0.0.1"), "failure changed selection\n");
    address.sin_addr.s_addr = inet_addr("239.255.0.1");
    address.sin_port = htons(49871);
    result = sendto(socket, "multicast", 9, 0, (struct sockaddr *)&address, sizeof(address));
    ok(result == 9, "multicast send: %d/%d\n", result, WSAGetLastError());
    value = 0;
    result = setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&value, sizeof(value));
    ok(!result, "clear selection: %d\n", WSAGetLastError());
    closesocket(socket);
done:
    WSACleanup();
}

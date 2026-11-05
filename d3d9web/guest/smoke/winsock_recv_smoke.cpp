#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstring>

int main()
{
    WSADATA data = {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { std::printf("D3D9WEB_DIRECT_RECV_FAIL startup=%d\n", WSAGetLastError()); return 1; }

    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    // Browser SOCKFS connects through the local WebSocket-to-TCP bridge.
    // 19200 is the bridge's raw TCP echo endpoint; 19201 is its WebSocket
    // listener and therefore the address a browser guest must use.
    address.sin_port = htons(19201);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (socket_handle == INVALID_SOCKET || connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) { std::printf("D3D9WEB_DIRECT_RECV_FAIL connect=%d\n", WSAGetLastError()); return 2; }

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(socket_handle, &write_set);
    timeval connect_timeout = {5, 0};
    const int connect_ready = select(0, nullptr, &write_set, nullptr, &connect_timeout);
    if (connect_ready != 1) { std::printf("D3D9WEB_DIRECT_RECV_FAIL connect_wait=%d error=%d\n", connect_ready, WSAGetLastError()); return 21; }

    WSAEVENT read_event = WSACreateEvent();
    if (read_event == WSA_INVALID_EVENT || WSAEventSelect(socket_handle, read_event, FD_READ | FD_CLOSE) != 0) { std::printf("D3D9WEB_DIRECT_RECV_FAIL select=%d\n", WSAGetLastError()); return 3; }

    const char payload[] = "AZROK_WINSOCK_DIRECT_RECV";
    const int payload_bytes = static_cast<int>(sizeof(payload) - 1);
    const int sent = send(socket_handle, payload, payload_bytes, 0);
    if (sent != payload_bytes) { std::printf("D3D9WEB_DIRECT_RECV_FAIL send=%d error=%d\n", sent, WSAGetLastError()); return 4; }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket_handle, &read_set);
    timeval timeout = {5, 0};
    const int select_result = select(0, &read_set, nullptr, nullptr, &timeout);
    const int select_error = WSAGetLastError();

    char received[sizeof(payload)] = {};
    const int received_bytes = select_result > 0 ? recv(socket_handle, received, payload_bytes, 0) : SOCKET_ERROR;
    const int last_error = received_bytes > 0 ? 0 : WSAGetLastError();
    const bool match = received_bytes == payload_bytes && !memcmp(received, payload, payload_bytes);
    std::printf("D3D9WEB_SELECT select=%d select_error=%d bytes=%d error=%d match=%d\n",
                select_result, select_error, received_bytes, last_error, match ? 1 : 0);
    WSACloseEvent(read_event);
    closesocket(socket_handle);
    WSACleanup();
    return match ? 0 : 5;
}

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main() {
    printf("D3D9WEB_WINSOCK_PID=%lu\n", static_cast<unsigned long>(GetCurrentProcessId()));
    WSADATA data = {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        printf("D3D9WEB_WINSOCK_FAIL step=startup error=%d\n", WSAGetLastError());
        return 1;
    }

    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET) {
        printf("D3D9WEB_WINSOCK_FAIL step=socket error=%d\n", WSAGetLastError());
        WSACleanup();
        return 2;
    }

    sockaddr_in target = {};
    target.sin_family = AF_INET;
    target.sin_port = htons(19201);
    target.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(socket_handle, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0) {
        printf("D3D9WEB_WINSOCK_FAIL step=connect error=%d\n", WSAGetLastError());
        closesocket(socket_handle);
        WSACleanup();
        return 3;
    }

    WSAEVENT read_event = WSACreateEvent();
    if (read_event == WSA_INVALID_EVENT) {
        printf("D3D9WEB_WINSOCK_FAIL step=create_event error=%d\n", WSAGetLastError());
        closesocket(socket_handle);
        WSACleanup();
        return 4;
    }
    const int event_select_result = WSAEventSelect(socket_handle, read_event, FD_READ | FD_CLOSE);
    const int event_select_error = WSAGetLastError();
    printf("D3D9WEB_WINSOCK_EVENT_SELECT result=%d error=%d\n", event_select_result, event_select_error);
    if (event_select_result != 0) {
        printf("D3D9WEB_WINSOCK_FAIL step=event_select error=%d\n", event_select_error);
        WSACloseEvent(read_event);
        closesocket(socket_handle);
        WSACleanup();
        return 5;
    }
    printf("D3D9WEB_WINSOCK_HANDLES socket=0x%08lX event=0x%08lX mask=0x%08X\n",
           static_cast<unsigned long>(socket_handle), static_cast<unsigned long>(reinterpret_cast<uintptr_t>(read_event)),
           static_cast<unsigned>(FD_READ | FD_CLOSE));

    const char payload[] = "AZROK_WINSOCK_BROWSER_SMOKE_0123456789";
    const int payload_bytes = static_cast<int>(sizeof(payload) - 1);
    if (send(socket_handle, payload, payload_bytes, 0) != payload_bytes) {
        printf("D3D9WEB_WINSOCK_FAIL step=send error=%d\n", WSAGetLastError());
        WSACloseEvent(read_event);
        closesocket(socket_handle);
        WSACleanup();
        return 6;
    }

    const DWORD wait_result = WSAWaitForMultipleEvents(1, &read_event, FALSE, 5000, FALSE);
    if (wait_result != WSA_WAIT_EVENT_0) {
        WSANETWORKEVENTS timeout_events = {};
        const int timeout_enum = WSAEnumNetworkEvents(socket_handle, read_event, &timeout_events);
        printf("D3D9WEB_WINSOCK_POST_TIMEOUT_ENUM result=%d events=0x%08lX read_error=%d close_error=%d api_error=%d\n",
               timeout_enum, static_cast<unsigned long>(timeout_events.lNetworkEvents),
               timeout_events.iErrorCode[FD_READ_BIT], timeout_events.iErrorCode[FD_CLOSE_BIT], WSAGetLastError());
        printf("D3D9WEB_WINSOCK_FAIL step=wait result=0x%08lX error=%d\n",
               static_cast<unsigned long>(wait_result), WSAGetLastError());
        WSACloseEvent(read_event);
        closesocket(socket_handle);
        WSACleanup();
        return 7;
    }

    WSANETWORKEVENTS network_events = {};
    if (WSAEnumNetworkEvents(socket_handle, read_event, &network_events) != 0 ||
        !(network_events.lNetworkEvents & FD_READ)) {
        printf("D3D9WEB_WINSOCK_FAIL step=enum events=0x%08lX read_error=%d close_error=%d api_error=%d\n",
               static_cast<unsigned long>(network_events.lNetworkEvents),
               network_events.iErrorCode[FD_READ_BIT], network_events.iErrorCode[FD_CLOSE_BIT],
               WSAGetLastError());
        WSACloseEvent(read_event);
        closesocket(socket_handle);
        WSACleanup();
        return 8;
    }

    char received[sizeof(payload)] = {};
    int total = 0;
    while (total < payload_bytes) {
        int count = recv(socket_handle, received + total, payload_bytes - total, 0);
        if (count <= 0) {
            printf("D3D9WEB_WINSOCK_FAIL step=recv bytes=%d error=%d\n", count, WSAGetLastError());
            WSACloseEvent(read_event);
            closesocket(socket_handle);
            WSACleanup();
            return 9;
        }
        total += count;
    }

    const bool match = total == payload_bytes && memcmp(received, payload, payload_bytes) == 0;
    printf("D3D9WEB_WINSOCK_RESULT connect=1 event_select=1 wait=1 enum=0x%08lX send=%d recv=%d match=%d\n",
           static_cast<unsigned long>(network_events.lNetworkEvents), payload_bytes, total, match ? 1 : 0);
    WSACloseEvent(read_event);
    closesocket(socket_handle);
    WSACleanup();
    return match ? 0 : 10;
}

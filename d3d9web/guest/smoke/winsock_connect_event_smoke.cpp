#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cstring>

int main() {
    WSADATA data = {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;

    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    WSAEVENT eventHandle = WSACreateEvent();
    if (socketHandle == INVALID_SOCKET || eventHandle == WSA_INVALID_EVENT) return 2;

    // WSAEventSelect is intentionally registered before connect.  This is the
    // game's non-blocking pattern and requires a real FD_CONNECT completion,
    // not merely the FD_READ path validated by the earlier smoke.
    const long requestedEvents = FD_CONNECT | FD_READ | FD_CLOSE;
    if (WSAEventSelect(socketHandle, eventHandle, requestedEvents) != 0) {
        std::printf("D3D9WEB_CONNECT_EVENT_FAIL step=event_select error=%d\n", WSAGetLastError());
        return 3;
    }

    sockaddr_in target = {};
    target.sin_family = AF_INET;
    target.sin_port = htons(19201);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connectResult = connect(socketHandle, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    const int connectError = connectResult == 0 ? 0 : WSAGetLastError();
    if (connectResult != 0 && connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS) {
        std::printf("D3D9WEB_CONNECT_EVENT_FAIL step=connect error=%d\n", connectError);
        return 4;
    }

    const DWORD connectWait = WSAWaitForMultipleEvents(1, &eventHandle, FALSE, 5000, FALSE);
    WSANETWORKEVENTS connectEvents = {};
    const int connectEnum = WSAEnumNetworkEvents(socketHandle, eventHandle, &connectEvents);
    const int connectEnumError = connectEnum == 0 ? 0 : WSAGetLastError();
    std::printf("D3D9WEB_CONNECT_EVENT_CONNECT wait=0x%08lX enum=%d enum_error=%d events=0x%08lX connect_error=%d initial=%d\n",
        static_cast<unsigned long>(connectWait), connectEnum,
        connectEnumError, static_cast<unsigned long>(connectEvents.lNetworkEvents),
        connectEvents.iErrorCode[FD_CONNECT_BIT], connectError);
    if (connectWait != WSA_WAIT_EVENT_0 || connectEnum != 0 ||
        !(connectEvents.lNetworkEvents & FD_CONNECT) || connectEvents.iErrorCode[FD_CONNECT_BIT] != 0) {
        return 5;
    }

    static const char payload[] = "AZROK_FD_CONNECT_BROWSER_SMOKE";
    if (send(socketHandle, payload, sizeof(payload) - 1, 0) != static_cast<int>(sizeof(payload) - 1)) {
        std::printf("D3D9WEB_CONNECT_EVENT_FAIL step=send error=%d\n", WSAGetLastError());
        return 6;
    }

    const DWORD readWait = WSAWaitForMultipleEvents(1, &eventHandle, FALSE, 5000, FALSE);
    WSANETWORKEVENTS readEvents = {};
    const int readEnum = WSAEnumNetworkEvents(socketHandle, eventHandle, &readEvents);
    const int readEnumError = readEnum == 0 ? 0 : WSAGetLastError();
    char received[sizeof(payload)] = {};
    const int receivedBytes = recv(socketHandle, received, sizeof(payload) - 1, 0);
    const bool match = receivedBytes == static_cast<int>(sizeof(payload) - 1) &&
        std::memcmp(payload, received, sizeof(payload) - 1) == 0;
    std::printf("D3D9WEB_CONNECT_EVENT_READ wait=0x%08lX enum=%d enum_error=%d events=0x%08lX recv=%d match=%d\n",
        static_cast<unsigned long>(readWait), readEnum, readEnumError,
        static_cast<unsigned long>(readEvents.lNetworkEvents),
        receivedBytes, match ? 1 : 0);

    WSACloseEvent(eventHandle);
    closesocket(socketHandle);
    WSACleanup();
    return (readWait == WSA_WAIT_EVENT_0 && readEnum == 0 &&
            (readEvents.lNetworkEvents & FD_READ) && match) ? 0 : 7;
}

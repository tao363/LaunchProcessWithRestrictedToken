#include "NetworkFilterPlugin.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#undef WIN32_LEAN_AND_MEAN

#include <algorithm>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

NetworkFilterState NetworkFilterPlugin::g_State;

bool NetworkFilterPlugin::Initialize(int port) {
    if (g_State.running) {
        LogWarn(L"[NetworkFilterPlugin] Already running, ignoring Initialize.");
        return true;
    }

    g_State.port = (port > 0 && port <= 65535) ? port : 8080;
    g_State.running = false;
    g_State.listenSocket = nullptr;
    g_State.serverThread = nullptr;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LogError(L"[NetworkFilterPlugin] WSAStartup failed: %d", WSAGetLastError());
        return false;
    }

    LogInfo(L"[NetworkFilterPlugin] Winsock initialized.");

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        LogError(L"[NetworkFilterPlugin] socket() failed: %d", WSAGetLastError());
        WSACleanup();
        return false;
    }

    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    addr.sin_port = htons(static_cast<u_short>(g_State.port));

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        LogError(L"[NetworkFilterPlugin] bind() failed on port %d: %d", g_State.port, err);
        closesocket(listenSock);
        WSACleanup();
        return false;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        LogError(L"[NetworkFilterPlugin] listen() failed: %d", WSAGetLastError());
        closesocket(listenSock);
        WSACleanup();
        return false;
    }

    g_State.listenSocket = (void*)listenSock;

    g_State.serverThread = CreateThread(nullptr, 0, ProxyServerThread, nullptr, 0, nullptr);
    if (!g_State.serverThread) {
        LogError(L"[NetworkFilterPlugin] CreateThread failed: %d", GetLastError());
        closesocket(listenSock);
        WSACleanup();
        return false;
    }

    g_State.running = true;
    LogInfo(L"[NetworkFilterPlugin] Initialized and listening on 127.0.0.1:%d", g_State.port);
    LogProxyInfo(L"========== Proxy started on 127.0.0.1:%d ==========", g_State.port);
    return true;
}

void NetworkFilterPlugin::Shutdown() {
    if (!g_State.running) {
        return;
    }

    LogInfo(L"[NetworkFilterPlugin] Shutting down...");

    g_State.running = false;

    if (g_State.listenSocket != nullptr) {
        closesocket((SOCKET)g_State.listenSocket);
        g_State.listenSocket = nullptr;
    }

    if (g_State.serverThread) {
        WaitForSingleObject(g_State.serverThread, 5000);
        CloseHandle(g_State.serverThread);
        g_State.serverThread = nullptr;
    }

    int waitCount = 0;
    while (g_State.activeConnections > 0 && waitCount < 30) {
        Sleep(100);
        waitCount++;
    }
    if (g_State.activeConnections > 0) {
        LogWarn(L"[NetworkFilterPlugin] %d active connections still pending after shutdown timeout",
            g_State.activeConnections.load());
    }

    WSACleanup();

    LogInfo(L"[NetworkFilterPlugin] Shutdown complete.");
    LogProxyInfo(L"========== Proxy stopped ==========");
}

void NetworkFilterPlugin::SetAllowedDomains(const std::vector<std::wstring>& domains) {
    std::lock_guard<std::mutex> lock(g_State.mutex);

    g_State.allowedDomains.clear();

    const wchar_t* modeStr = (g_State.filterMode == FilterMode::Whitelist) ? L"Whitelist" : L"Blacklist";

    for (const auto& domain : domains) {
        DomainPattern dp;
        dp.pattern = domain;
        dp.hasWildcard = (domain.find(L'*') != std::wstring::npos);

        g_State.allowedDomains.push_back(dp);
        LogInfo(L"[NetworkFilterPlugin] [%ls] Domain: %ls (wildcard=%d)",
            modeStr, domain.c_str(), dp.hasWildcard ? 1 : 0);
    }

    LogInfo(L"[NetworkFilterPlugin] Total %llu domains configured in %ls mode.",
        (unsigned long long)g_State.allowedDomains.size(), modeStr);

    LogProxyInfo(L"--- %ls domain rules (%llu) ---",
        modeStr, (unsigned long long)g_State.allowedDomains.size());
    for (const auto& dp : g_State.allowedDomains) {
        LogProxyInfo(L"  %ls %ls", dp.hasWildcard ? L"[wildcard]" : L"  [exact]", dp.pattern.c_str());
    }
    LogProxyInfo(L"--- End of domain rules ---");
}

void NetworkFilterPlugin::SetFilterMode(FilterMode mode) {
    std::lock_guard<std::mutex> lock(g_State.mutex);
    g_State.filterMode = mode;
    const wchar_t* modeStr = (mode == FilterMode::Whitelist) ? L"Whitelist" : L"Blacklist";
    LogInfo(L"[NetworkFilterPlugin] Filter mode set to: %ls", modeStr);
    LogProxyInfo(L"========== Filter mode: %ls ==========", modeStr);
}

bool NetworkFilterPlugin::IsRunning() {
    return g_State.running;
}

int NetworkFilterPlugin::GetProxyPort() {
    return g_State.port;
}

std::wstring NetworkFilterPlugin::GetProxyUrl() {
    return L"http://127.0.0.1:" + std::to_wstring(g_State.port);
}

bool NetworkFilterPlugin::MatchDomainPattern(const std::wstring& pattern, const std::wstring& domain) {
    if (pattern.empty() || domain.empty()) {
        return false;
    }

    if (pattern == L"*") {
        return true;
    }

    size_t wildcardPos = pattern.find(L'*');

    if (wildcardPos == std::wstring::npos) {
        return _wcsicmp(pattern.c_str(), domain.c_str()) == 0;
    }

    if (wildcardPos != 0 || pattern.size() < 2) {
        LogWarn(L"[NetworkFilterPlugin] Invalid wildcard pattern: %ls", pattern.c_str());
        return false;
    }

    std::wstring suffix = pattern.substr(1);

    if (domain.size() < suffix.size()) {
        return false;
    }

    std::wstring domainSuffix = domain.substr(domain.size() - suffix.size());

    return _wcsicmp(suffix.c_str(), domainSuffix.c_str()) == 0;
}

std::wstring NetworkFilterPlugin::StringToWide(const std::string& s) {
    if (s.empty()) return L"";

    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";

    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &result[0], size);

    return result;
}

std::string NetworkFilterPlugin::WideToString(const std::wstring& s) {
    if (s.empty()) return "";

    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";

    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &result[0], size, nullptr, nullptr);

    return result;
}

bool NetworkFilterPlugin::ParseRequestLine(const std::string& line,
    std::string& method,
    std::string& url,
    std::string& version) {
    method.clear();
    url.clear();
    version.clear();

    size_t pos1 = line.find(' ');
    if (pos1 == std::string::npos) return false;

    size_t pos2 = line.find(' ', pos1 + 1);
    if (pos2 == std::string::npos) return false;

    method = line.substr(0, pos1);
    url = line.substr(pos1 + 1, pos2 - pos1 - 1);
    version = line.substr(pos2 + 1);

    return true;
}

// [FIX] ԭ ExtractDomainFromUrl ���ڵ�һ�� ':' ���ضϣ����¶˿ڶ�ʧ
// �°汾���������� host:port ���֣�ֻȥ�� scheme �� path
// CONNECT "example.com:443"            �� "example.com:443"
// GET     "http://example.com/path"    �� "example.com"
// GET     "http://example.com:8080/p"  �� "example.com:8080"
std::string NetworkFilterPlugin::ExtractHostPortFromUrl(const std::string& url) {
    if (url.empty()) return "";

    size_t start = 0;

    size_t protoPos = url.find("://");
    if (protoPos != std::string::npos) {
        start = protoPos + 3;
    }

    if (start >= url.size()) return "";

    // [FIX] ֻ�� '/' ��Ϊ������������ host:port �е�ð��
    size_t end = url.find('/', start);
    if (end == std::string::npos) {
        end = url.size();
    }

    if (end <= start) return "";

    return url.substr(start, end - start);
}

// [FIX] �� "host:port" �ַ����з���� host �� port
void NetworkFilterPlugin::SplitHostPort(const std::string& hostPort,
    std::string& host, int& port,
    int defaultPort) {
    host = hostPort;
    port = defaultPort;

    if (hostPort.empty()) return;

    // IPv6 ��ַ: [::1]:port
    if (hostPort[0] == '[') {
        size_t bracketEnd = hostPort.find(']');
        if (bracketEnd != std::string::npos) {
            host = hostPort.substr(1, bracketEnd - 1);  // ȥ��������
            if (bracketEnd + 1 < hostPort.size() && hostPort[bracketEnd + 1] == ':') {
                std::string portStr = hostPort.substr(bracketEnd + 2);
                bool allDigits = !portStr.empty();
                for (char c : portStr) {
                    if (c < '0' || c > '9') { allDigits = false; break; }
                }
                if (allDigits) {
                    try {
                        int parsedPort = std::stoi(portStr);
                        if (parsedPort > 0 && parsedPort <= 65535) {
                            port = parsedPort;
                        }
                    }
                    catch (...) {}
                }
            }
        }
        return;
    }

    // IPv4 / hostname: host:port
    size_t colonPos = hostPort.rfind(':');
    if (colonPos != std::string::npos && colonPos > 0) {
        std::string portStr = hostPort.substr(colonPos + 1);
        bool allDigits = !portStr.empty();
        for (char c : portStr) {
            if (c < '0' || c > '9') { allDigits = false; break; }
        }
        if (allDigits) {
            try {
                int parsedPort = std::stoi(portStr);
                if (parsedPort > 0 && parsedPort <= 65535) {
                    host = hostPort.substr(0, colonPos);
                    port = parsedPort;
                }
                else {
                    LogProxyWarn(L"Invalid port number %d in URL, using default %d", parsedPort, defaultPort);
                }
            }
            catch (...) {
                LogProxyWarn(L"Failed to parse port from URL, using default %d", defaultPort);
            }
        }
    }
}

std::string NetworkFilterPlugin::ReadLine(void* sock) {
    SOCKET s = (SOCKET)sock;
    std::string result;
    char ch;

    DWORD timeout = 30000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    while (true) {
        int recvResult = recv(s, &ch, 1, 0);
        if (recvResult <= 0) {
            break;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            break;
        }

        result += ch;

        if (result.size() > 8192) {
            break;
        }
    }

    return result;
}

bool NetworkFilterPlugin::SendErrorResponse(void* sock, int code, const char* status, const char* body) {
    SOCKET s = (SOCKET)sock;
    char response[512];
    int len = sprintf_s(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        code, status, strlen(body), body);

    if (len > 0) {
        send(s, response, len, 0);
    }

    return true;
}

bool NetworkFilterPlugin::ConnectToServer(const std::string& host, int port, void*& serverSocket) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    int ret = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (ret != 0) {
        LogError(L"[NetworkFilterPlugin] getaddrinfo failed for %hs:%d: %d", host.c_str(), port, ret);
        return false;
    }

    SOCKET serverSock = INVALID_SOCKET;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        serverSock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (serverSock == INVALID_SOCKET) {
            continue;
        }

        if (connect(serverSock, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
            closesocket(serverSock);
            serverSock = INVALID_SOCKET;
            continue;
        }

        break;
    }

    freeaddrinfo(result);

    if (serverSock == INVALID_SOCKET) {
        LogError(L"[NetworkFilterPlugin] Failed to connect to %hs:%d", host.c_str(), port);
        return false;
    }

    serverSocket = (void*)serverSock;
    return true;
}

bool NetworkFilterPlugin::HandleClientConnection(void* clientSocket) {
    g_State.activeConnections++;
    SOCKET client = (SOCKET)clientSocket;

    auto cleanup = [&]() {
        closesocket(client);
        g_State.activeConnections--;
        };

    std::string requestLine = ReadLine(clientSocket);
    if (requestLine.empty()) {
        cleanup();
        return false;
    }

    std::string method, url, version;
    if (!ParseRequestLine(requestLine, method, url, version)) {
        LogProxyError(L"Invalid request line from client");
        SendErrorResponse(clientSocket, 400, "Bad Request", "Invalid request line");
        cleanup();
        return false;
    }

    // [FIX] ʹ���º�����ȡ���� host:port
    std::string hostPort = ExtractHostPortFromUrl(url);
    if (hostPort.empty()) {
        LogProxyError(L"Could not extract host from URL");
        SendErrorResponse(clientSocket, 400, "Bad Request", "Invalid URL");
        cleanup();
        return false;
    }

    // [FIX] CONNECT Ĭ�� 443������ HTTP ����Ĭ�� 80
    bool isConnect = (_stricmp(method.c_str(), "CONNECT") == 0);
    int defaultPort = isConnect ? 443 : 80;

    std::string host;
    int port = defaultPort;
    SplitHostPort(hostPort, host, port, defaultPort);

    // �ò����˿ڵĴ�������������ƥ��
    std::wstring domainW = StringToWide(host);

    bool matched = false;
    std::wstring matchedPatternStr;
    {
        std::lock_guard<std::mutex> lock(g_State.mutex);
        for (const auto& pattern : g_State.allowedDomains) {
            if (MatchDomainPattern(pattern.pattern, domainW)) {
                matchedPatternStr = pattern.pattern;
                matched = true;
                break;
            }
        }
    }

    std::wstring methodW = StringToWide(method);

    // 根据过滤模式决定是否允许访问
    bool allowed = false;
    if (g_State.filterMode == FilterMode::Whitelist) {
        // 白名单模式：匹配则允许，不匹配则阻止
        allowed = matched;
    } else {
        // 黑名单模式：匹配则阻止，不匹配则允许
        allowed = !matched;
    }

    if (allowed) {
        if (g_State.filterMode == FilterMode::Whitelist) {
            LogProxyAllow(methodW.c_str(), domainW.c_str(), matchedPatternStr.c_str());
        } else {
            LogProxyAllow(methodW.c_str(), domainW.c_str(), L"not in blacklist");
        }
    }
    else {
        if (g_State.filterMode == FilterMode::Whitelist) {
            LogProxyBlock(methodW.c_str(), domainW.c_str(), L"not in whitelist");
        } else {
            LogProxyBlock(methodW.c_str(), domainW.c_str(), matchedPatternStr.c_str());
        }
        SendErrorResponse(clientSocket, 403, "Forbidden",
            "Access to this domain is not allowed by network filter");
        cleanup();
        return false;
    }

    if (isConnect) {
        void* serverSocket = nullptr;
        if (!ConnectToServer(host, port, serverSocket)) {
            LogProxyError(L"CONNECT tunnel failed: cannot reach %hs:%d", host.c_str(), port);
            SendErrorResponse(clientSocket, 502, "Bad Gateway",
                "Failed to connect to target server");
            cleanup();
            return false;
        }

        std::string connectResponse = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send(client, connectResponse.c_str(), (int)connectResponse.size(), 0);

        HandleHttpsTunnel(clientSocket, serverSocket);

        closesocket((SOCKET)serverSocket);
    }
    else {
        void* serverSocket = nullptr;
        if (!ConnectToServer(host, port, serverSocket)) {
            LogProxyError(L"HTTP forward failed: cannot reach %hs:%d", host.c_str(), port);
            SendErrorResponse(clientSocket, 502, "Bad Gateway",
                "Failed to connect to target server");
            cleanup();
            return false;
        }

        SOCKET server = (SOCKET)serverSocket;
        std::string fullRequest = requestLine + "\r\n";

        // ��ȡ����ͷ, ͬʱ���� Content-Length �� Transfer-Encoding
        long long contentLength = -1;
        bool chunkedEncoding = false;

        std::string headerLine;
        while (!(headerLine = ReadLine(clientSocket)).empty()) {
            fullRequest += headerLine + "\r\n";

            if (_strnicmp(headerLine.c_str(), "Content-Length:", 15) == 0) {
                const char* val = headerLine.c_str() + 15;
                while (*val == ' ' || *val == '\t') val++;
                try {
                    contentLength = std::stoll(val);
                }
                catch (...) {
                    contentLength = -1;
                }
            }

            if (_strnicmp(headerLine.c_str(), "Transfer-Encoding:", 18) == 0) {
                std::string val = headerLine.substr(18);
                size_t start = val.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    val = val.substr(start);
                }
                std::string valLower = val;
                std::transform(valLower.begin(), valLower.end(),
                    valLower.begin(), ::tolower);
                if (valLower.find("chunked") != std::string::npos) {
                    chunkedEncoding = true;
                }
            }
        }
        fullRequest += "\r\n";

        // ��������ͷ
        send(server, fullRequest.c_str(), (int)fullRequest.size(), 0);

        // ת��������
        if (chunkedEncoding) {
            ForwardChunkedBody(clientSocket, serverSocket);
        }
        else if (contentLength > 0) {
            ForwardRequestBody(clientSocket, serverSocket, contentLength);
        }

        // ��ȡ��������Ӧ��ת���ؿͻ���
        ForwardData(serverSocket, clientSocket);

        closesocket(server);
    }

    cleanup();
    return true;
}

bool NetworkFilterPlugin::ForwardRequestBody(void* src, void* dst, long long contentLength) {
    SOCKET s = (SOCKET)src;
    SOCKET d = (SOCKET)dst;
    char buffer[8192];

    long long remaining = contentLength;
    while (remaining > 0 && g_State.running) {
        int toRead = (remaining > (long long)sizeof(buffer))
            ? (int)sizeof(buffer)
            : (int)remaining;

        int recvLen = recv(s, buffer, toRead, 0);
        if (recvLen <= 0) {
            if (recvLen < 0) {
                LogProxyWarn(L"ForwardRequestBody recv() failed: %d", WSAGetLastError());
            }
            return false;
        }

        int sent = 0;
        while (sent < recvLen) {
            int sendResult = send(d, buffer + sent, recvLen - sent, 0);
            if (sendResult <= 0) {
                LogProxyWarn(L"ForwardRequestBody send() failed: %d", WSAGetLastError());
                return false;
            }
            sent += sendResult;
        }

        remaining -= recvLen;
    }

    return (remaining == 0);
}

bool NetworkFilterPlugin::ForwardChunkedBody(void* src, void* dst) {
    SOCKET d = (SOCKET)dst;
    char buffer[8192];

    while (g_State.running) {
        // 1) ��ȡ chunk ��С��
        std::string sizeLine = ReadLine(src);
        std::string sizeLineRaw = sizeLine + "\r\n";
        if (send(d, sizeLineRaw.c_str(), (int)sizeLineRaw.size(), 0) <= 0) {
            LogProxyWarn(L"ForwardChunkedBody send chunk-size failed: %d", WSAGetLastError());
            return false;
        }

        // 2) ���� chunk ��С
        long long chunkSize = 0;
        try {
            std::string hexPart = sizeLine;
            size_t semiPos = hexPart.find(';');
            if (semiPos != std::string::npos) {
                hexPart = hexPart.substr(0, semiPos);
            }
            while (!hexPart.empty() && (hexPart.back() == ' ' || hexPart.back() == '\t')) {
                hexPart.pop_back();
            }
            chunkSize = std::stoll(hexPart, nullptr, 16);
        }
        catch (...) {
            LogProxyWarn(L"ForwardChunkedBody: failed to parse chunk size");
            return false;
        }

        // 3) ��ֹ chunk
        if (chunkSize == 0) {
            std::string trailerLine;
            while (!(trailerLine = ReadLine(src)).empty()) {
                std::string raw = trailerLine + "\r\n";
                send(d, raw.c_str(), (int)raw.size(), 0);
            }
            const char* endBlock = "\r\n";
            send(d, endBlock, 2, 0);
            return true;
        }

        // 4) ��ȡ��ת�� chunk ����
        SOCKET s = (SOCKET)src;
        long long remaining = chunkSize;
        while (remaining > 0) {
            int toRead = (remaining > (long long)sizeof(buffer))
                ? (int)sizeof(buffer)
                : (int)remaining;
            int recvLen = recv(s, buffer, toRead, 0);
            if (recvLen <= 0) {
                LogProxyWarn(L"ForwardChunkedBody recv chunk data failed: %d", WSAGetLastError());
                return false;
            }
            int sent = 0;
            while (sent < recvLen) {
                int sendResult = send(d, buffer + sent, recvLen - sent, 0);
                if (sendResult <= 0) {
                    LogProxyWarn(L"ForwardChunkedBody send chunk data failed: %d", WSAGetLastError());
                    return false;
                }
                sent += sendResult;
            }
            remaining -= recvLen;
        }

        // 5) chunk ���ݺ�� \r\n
        std::string chunkEnd = ReadLine(src);
        std::string chunkEndRaw = chunkEnd + "\r\n";
        send(d, chunkEndRaw.c_str(), (int)chunkEndRaw.size(), 0);
    }

    return false;
}

bool NetworkFilterPlugin::HandleHttpsTunnel(void* client, void* server) {
    SOCKET c = (SOCKET)client;
    SOCKET s = (SOCKET)server;
    char buffer[4096];

    while (g_State.running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(c, &readSet);
        FD_SET(s, &readSet);

        SOCKET maxFd = (c > s) ? c : s;
        timeval timeout;
        timeout.tv_sec = 300;
        timeout.tv_usec = 0;
        int selectResult = select((int)maxFd + 1, &readSet, nullptr, nullptr, &timeout);

        if (selectResult == 0) {
            LogProxyWarn(L"HTTPS tunnel timeout after 300 seconds of inactivity");
            break;
        }

        if (selectResult < 0) {
            int err = WSAGetLastError();
            LogProxyWarn(L"HTTPS tunnel select() error: %d", err);
            break;
        }

        if (FD_ISSET(c, &readSet)) {
            int recvLen = recv(c, buffer, sizeof(buffer), 0);
            if (recvLen <= 0) break;
            int sendResult = send(s, buffer, recvLen, 0);
            if (sendResult <= 0) {
                LogProxyWarn(L"HTTPS tunnel send() to server failed: %d", WSAGetLastError());
                break;
            }
        }

        if (FD_ISSET(s, &readSet)) {
            int recvLen = recv(s, buffer, sizeof(buffer), 0);
            if (recvLen <= 0) break;
            int sendResult = send(c, buffer, recvLen, 0);
            if (sendResult <= 0) {
                LogProxyWarn(L"HTTPS tunnel send() to client failed: %d", WSAGetLastError());
                break;
            }
        }
    }

    return true;
}

bool NetworkFilterPlugin::ForwardData(void* src, void* dst) {
    SOCKET s = (SOCKET)src;
    SOCKET d = (SOCKET)dst;
    char buffer[8192];

    while (g_State.running) {
        int recvLen = recv(s, buffer, sizeof(buffer), 0);
        if (recvLen <= 0) {
            if (recvLen < 0) {
                LogProxyWarn(L"ForwardData recv() failed: %d", WSAGetLastError());
            }
            break;
        }

        int sent = 0;
        while (sent < recvLen) {
            int sendResult = send(d, buffer + sent, recvLen - sent, 0);
            if (sendResult <= 0) {
                LogProxyWarn(L"ForwardData send() failed: %d", WSAGetLastError());
                break;
            }
            sent += sendResult;
        }

        if (sent < recvLen) {
            break;
        }
    }

    return true;
}

// [FIX] ÿ�����Ӷ����߳����
unsigned long __stdcall NetworkFilterPlugin::ClientConnectionThread(void* param) {
    SOCKET clientSocket = (SOCKET)(uintptr_t)param;
    HandleClientConnection((void*)clientSocket);
    return 0;
}

unsigned long __stdcall NetworkFilterPlugin::ProxyServerThread(void* param) {
    (void)param;
    LogInfo(L"[NetworkFilterPlugin] Proxy server thread started.");

    while (g_State.running) {
        sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept((SOCKET)g_State.listenSocket, (sockaddr*)&clientAddr, &addrLen);

        if (!g_State.running) {
            if (clientSocket != INVALID_SOCKET) {
                closesocket(clientSocket);
            }
            break;
        }

        if (clientSocket == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err != WSAEINTR) {
                LogError(L"[NetworkFilterPlugin] accept() failed: %d", err);
            }
            continue;
        }

        DWORD clientIp = clientAddr.sin_addr.s_addr;
        LogDebug(L"[NetworkFilterPlugin] Client connected from %d.%d.%d.%d",
            clientIp & 0xFF, (clientIp >> 8) & 0xFF,
            (clientIp >> 16) & 0xFF, (clientIp >> 24) & 0xFF);

        // [FIX] ÿ�����Ӵ��������̣߳��������� accept ѭ��
        HANDLE hThread = CreateThread(nullptr, 0, ClientConnectionThread,
            (void*)(uintptr_t)clientSocket, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);  // �����̣߳����н���
        }
        else {
            LogError(L"[NetworkFilterPlugin] CreateThread for client failed: %d", GetLastError());
            closesocket(clientSocket);
        }
    }

    LogInfo(L"[NetworkFilterPlugin] Proxy server thread exiting.");
    return 0;
}


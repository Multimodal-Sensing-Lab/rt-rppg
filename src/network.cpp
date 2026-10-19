#include "network.h"
#include "timinglog.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <system_error>

namespace {

const int MAX_CLIENTS = 16;             // simultaneous connections, API requests and streams together
const size_t MAX_REQUEST_SIZE = 8192;   // longest accepted request head
const int SOCKET_TIMEOUT_S = 3;         // a client that does not send or receive for this time is closed
const int ACCEPT_POLL_MS = 200;         // the accept loops check this often if the servers must stop
const char *BOUNDARY = "rtrppgframe";

#ifdef MSG_NOSIGNAL
const int SEND_FLAGS = MSG_NOSIGNAL;    // writing to a closed connection must not raise SIGPIPE
#else
const int SEND_FLAGS = 0;               // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

bool sendAll(int fd, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t n = ::send(fd, data, size, SEND_FLAGS);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool sendAll(int fd, const std::string &text)
{
    return sendAll(fd, text.data(), text.size());
}

// Reads the request head and returns the method and the path without the query string.
bool readRequest(int fd, std::string &method, std::string &path)
{
    std::string head;
    char buffer[1024];
    while (head.find("\r\n\r\n") == std::string::npos) {
        if (head.size() > MAX_REQUEST_SIZE)
            return false;
        ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        head.append(buffer, static_cast<size_t>(n));
    }

    // Request line: METHOD SP PATH SP VERSION
    std::string line = head.substr(0, head.find("\r\n"));
    size_t a = line.find(' ');
    size_t b = (a == std::string::npos) ? a : line.find(' ', a + 1);
    if (a == std::string::npos || b == std::string::npos)
        return false;
    method = line.substr(0, a);
    path = line.substr(a + 1, b - a - 1);
    size_t query = path.find('?');
    if (query != std::string::npos)
        path.erase(query);
    return true;
}

void reply(int fd, const std::string &method, int status, const char *reason, const char *contentType,
           const std::string &body, const char *extraHeaders = "")
{
    std::ostringstream r;
    r << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
      << "Content-Type: " << contentType << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Cache-Control: no-store\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Connection: close\r\n"
      << extraHeaders << "\r\n";
    if (method != "HEAD")
        r << body;
    sendAll(fd, r.str());
}

// Same as printf("%0<width>.1f"), but always with a decimal point: QApplication sets the system locale,
// and some locales would write a decimal comma inside the comma-separated values.
std::string decimal(float value, int width = 0)
{
    if (!std::isfinite(value))
        return "nan";
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(1) << std::setfill('0') << std::internal << std::setw(width) << value;
    return s.str();
}

std::string jsonNumber(float value)
{
    return std::isfinite(value) ? decimal(value) : "null";
}

std::int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void setClientOptions(int fd)
{
    timeval timeout{SOCKET_TIMEOUT_S, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int openListener(const std::string &address, int port)
{
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "network: invalid IPv4 address " << address << std::endl;
        return -1;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "network: cannot create a socket: " << std::strerror(errno) << std::endl;
        return -1;
    }
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 || ::listen(fd, 16) < 0) {
        std::cerr << "network: cannot listen on " << address << ':' << port << ": " << std::strerror(errno) << std::endl;
        ::close(fd);
        return -1;
    }
    return fd;
}

// True if the client has closed the connection (checked while no frame is sent).
bool clientClosed(int fd)
{
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, 0) <= 0)
        return false;
    if (p.revents & (POLLHUP | POLLERR))
        return true;
    char c;
    ssize_t n = ::recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    return n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR);
}

int portFromEnvironment(const char *name, int defaultPort)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return defaultPort;
    char *end = nullptr;
    long port = std::strtol(value, &end, 10);
    if (*end != '\0' || port < 0 || port > 65535) {
        std::cerr << "network: ignoring " << name << '=' << value << ", using port " << defaultPort << std::endl;
        return defaultPort;
    }
    return static_cast<int>(port);
}

} // namespace


network::Settings network::settingsFromEnvironment()
{
    Settings s;
    const char *enabled = std::getenv("RT_RPPG_NETWORK");
    if (enabled != nullptr && std::string(enabled) == "0")
        s.enabled = false;
    const char *address = std::getenv("RT_RPPG_BIND");
    if (address != nullptr && *address != '\0')
        s.bindAddress = address;
    s.restPort = portFromEnvironment("RT_RPPG_REST_PORT", s.restPort);
    s.streamPort = portFromEnvironment("RT_RPPG_STREAM_PORT", s.streamPort);
    return s;
}


network::~network()
{
    stopNetwork();
}


bool network::initNetwork(const Settings &s)
{
    stopNetwork();
    settings = s;
    if (!settings.enabled) {
        std::cerr << "network: disabled" << std::endl;
        return true;
    }
    running = true;
    bool restStarted = startListener(restServer, settings.restPort, true);
    bool streamStarted = startListener(streamServer, settings.streamPort, false);
    return restStarted && streamStarted;
}


bool network::startListener(Listener &listener, int port, bool rest)
{
    if (port == 0)
        return true;
    listener.fd = openListener(settings.bindAddress, port);
    if (listener.fd < 0)
        return false;
    listener.thread = std::thread(&network::acceptClients, this, std::ref(listener), rest);
    std::cerr << "network: " << (rest ? "RESTful API at http://" : "video streams at http://")
              << settings.bindAddress << ':' << port << (rest ? "/vhealth" : "/") << std::endl;
    return true;
}


void network::stopNetwork()
{
    if (!running.exchange(false))
        return;

    for (Listener *listener : {&restServer, &streamServer}) {
        if (listener->thread.joinable())
            listener->thread.join();
        if (listener->fd >= 0) {
            ::close(listener->fd);
            listener->fd = -1;
        }
    }

    for (StreamBuffer &stream : streams) {
        std::lock_guard<std::mutex> lock(stream.m);
        stream.newFrame.notify_all();
    }

    // Wake up the client threads blocked in recv or send and wait until they finish
    std::unique_lock<std::mutex> lock(clientsMutex);
    for (int fd : clientSockets)
        ::shutdown(fd, SHUT_RDWR);
    clientsDone.wait(lock, [this] { return clientSockets.empty(); });
}


void network::acceptClients(Listener &listener, bool rest)
{
    timinglog::nameThread(rest ? "rt-net-rest" : "rt-net-stream");
    while (running) {
        pollfd p{listener.fd, POLLIN, 0};
        if (::poll(&p, 1, ACCEPT_POLL_MS) <= 0)
            continue;
        int fd = ::accept(listener.fd, nullptr, nullptr);
        if (fd < 0)
            continue;
        setClientOptions(fd);
        if (!addClient(fd)) {
            reply(fd, "GET", 503, "Service Unavailable", "text/plain", "Too many clients\n");
            ::close(fd);
            continue;
        }
        try {
            std::thread([this, fd, rest] {
                timinglog::nameThread("rt-net-client");
                serveClient(fd, rest);
                removeClient(fd);
            }).detach();
        } catch (const std::system_error &) {
            removeClient(fd);
        }
    }
}


bool network::addClient(int fd)
{
    std::lock_guard<std::mutex> lock(clientsMutex);
    if (!running || static_cast<int>(clientSockets.size()) >= MAX_CLIENTS)
        return false;
    clientSockets.insert(fd);
    return true;
}


void network::removeClient(int fd)
{
    // The socket is closed while the mutex is held, so stopNetwork never shuts down a reused descriptor
    std::lock_guard<std::mutex> lock(clientsMutex);
    clientSockets.erase(fd);
    ::close(fd);
    clientsDone.notify_all();
}


void network::serveClient(int fd, bool rest)
{
    std::string method, path;
    if (!readRequest(fd, method, path))
        return;
    if (method != "GET" && method != "HEAD") {
        reply(fd, method, 405, "Method Not Allowed", "text/plain", "Only GET and HEAD are supported\n",
              "Allow: GET, HEAD\r\n");
        return;
    }

    if (rest) {
        if (path == "/vhealth")
            reply(fd, method, 200, "OK", "text/plain", vitalSignsText());
        else if (path == "/vhealth.json")
            reply(fd, method, 200, "OK", "application/json", vitalSignsJson());
        else
            reply(fd, method, 404, "Not Found", "text/plain", "Not found\n");
        return;
    }

    if (path == "/mainstream" || path == "/pulsestream") {
        if (method == "HEAD")
            reply(fd, method, 200, "OK", "multipart/x-mixed-replace", "");
        else
            serveStream(fd, path == "/mainstream" ? MAIN_STREAM : PULSE_STREAM);
    } else if (path == "/" || path == "/index.html") {
        reply(fd, method, 200, "OK", "text/html; charset=utf-8", indexPage());
    } else {
        reply(fd, method, 404, "Not Found", "text/plain", "Not found\n");
    }
}


void network::serveStream(int fd, Stream stream)
{
    StreamBuffer &buffer = streams[stream];
    std::ostringstream head;
    head << "HTTP/1.1 200 OK\r\n"
         << "Content-Type: multipart/x-mixed-replace; boundary=" << BOUNDARY << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Connection: close\r\n\r\n";
    if (!sendAll(fd, head.str()))
        return;

    buffer.clients++;
    std::uint64_t sent = 0;
    while (running) {
        std::shared_ptr<const std::vector<unsigned char>> jpeg;
        std::uint64_t frame = 0;
        {
            std::unique_lock<std::mutex> lock(buffer.m);
            buffer.newFrame.wait_for(lock, std::chrono::milliseconds(500),
                                     [&] { return !running || buffer.count != sent; });
            if (running && buffer.count != sent && buffer.jpeg) {
                jpeg = buffer.jpeg;
                frame = buffer.frame;
                sent = buffer.count;
            }
        }
        if (!running)
            break;
        if (!jpeg) {
            if (clientClosed(fd))
                break;
            continue;
        }

        // Only the latest image is sent, so a slow client skips frames instead of delaying the others
        char part[160];
        int n = std::snprintf(part, sizeof(part),
                              "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\nX-Frame: %llu\r\n\r\n",
                              BOUNDARY, jpeg->size(), static_cast<unsigned long long>(frame));
        if (!sendAll(fd, part, static_cast<size_t>(n)) ||
            !sendAll(fd, reinterpret_cast<const char *>(jpeg->data()), jpeg->size()) ||
            !sendAll(fd, "\r\n", 2))
            break;
    }
    buffer.clients--;
}


void network::setVitalSigns(const VitalSigns &values)
{
    std::lock_guard<std::mutex> lock(vitalsMutex);
    vitals = values;
    vitalsTime = nowMs();
}


bool network::isStreaming(Stream stream) const
{
    return running && streams[stream].clients > 0;
}


void network::sendFrame(Stream stream, std::vector<unsigned char> jpeg, std::uint64_t frame)
{
    StreamBuffer &buffer = streams[stream];
    auto image = std::make_shared<const std::vector<unsigned char>>(std::move(jpeg));
    {
        std::lock_guard<std::mutex> lock(buffer.m);
        buffer.jpeg = std::move(image);
        buffer.frame = frame;
        buffer.count++;
    }
    buffer.newFrame.notify_all();
}


std::string network::vitalSignsText()
{
    VitalSigns v;
    {
        std::lock_guard<std::mutex> lock(vitalsMutex);
        v = vitals;
    }
    // Same format as the original API: "%05.1f,%05.1f,%05.1f,%d,%d,%08.1f,%08.1f"
    std::ostringstream s;
    s << decimal(v.hr, 5) << ',' << decimal(v.br, 5) << ',' << decimal(v.spo2, 5) << ','
      << v.seeUser << ',' << v.stable << ',' << decimal(v.hrGraph, 8) << ',' << decimal(v.brGraph, 8);
    return s.str();
}


std::string network::vitalSignsJson()
{
    VitalSigns v;
    std::int64_t time;
    {
        std::lock_guard<std::mutex> lock(vitalsMutex);
        v = vitals;
        time = vitalsTime;
    }
    std::ostringstream s;
    s << "{\"hr\":" << jsonNumber(v.hr) << ",\"rr\":" << jsonNumber(v.br) << ",\"spo2\":" << jsonNumber(v.spo2)
      << ",\"user_present\":" << v.seeUser << ",\"stable\":" << v.stable
      << ",\"hr_signal\":" << jsonNumber(v.hrGraph) << ",\"rr_signal\":" << jsonNumber(v.brGraph)
      << ",\"timestamp_ms\":" << time << "}\n";
    return s.str();
}


std::string network::indexPage() const
{
    // The values are read from the RESTful API, which allows cross-origin requests
    std::ostringstream page;
    page << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>rt-rppg</title>\n"
            "<style>body{font-family:sans-serif;margin:16px}img{max-width:100%;margin:4px 8px 4px 0;"
            "border:1px solid #888}</style>\n"
            "</head><body>\n<h1>Real-time rPPG</h1>\n<p id=\"values\">Waiting for the RESTful API...</p>\n"
            "<img src=\"/mainstream\" alt=\"Camera image\"><img src=\"/pulsestream\" alt=\"Pulsating face\">\n"
            "<p>RR and SpO2 are experimental outputs.</p>\n<script>\n";
    if (settings.restPort > 0)
        page << "const api = location.protocol + '//' + location.hostname + ':" << settings.restPort
             << "/vhealth.json';\n"
                "async function update() {\n"
                "  const out = document.getElementById('values');\n"
                "  try {\n"
                "    const v = await (await fetch(api, {cache: 'no-store'})).json();\n"
                "    out.textContent = !v.user_present ? 'No user in front of the camera' :\n"
                "      `HR ${v.hr} bpm, RR ${v.rr} breaths/min, SpO2 ${v.spo2} %` +\n"
                "      (v.stable ? '' : ' (not stable yet)');\n"
                "  } catch (e) {\n"
                "    out.textContent = 'The RESTful API is not available';\n"
                "  }\n"
                "}\n"
                "update();\nsetInterval(update, 1000);\n";
    else
        page << "document.getElementById('values').textContent = 'The RESTful API is disabled';\n";
    page << "</script>\n</body></html>\n";
    return page.str();
}

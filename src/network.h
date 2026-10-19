#ifndef NETWORK_H
#define NETWORK_H

/*
 * Network interface of the real-time rPPG application.
 *
 *  RESTful API (port 8080 by default)
 *    GET /vhealth       latest values as text, formatted as "%05.1f,%05.1f,%05.1f,%d,%d,%08.1f,%08.1f":
 *                       heart rate, breathing rate, SpO2, user present, stable, and the latest samples
 *                       of the pulse and breathing signals
 *    GET /vhealth.json  the same values as a JSON object
 *
 *  HTTP streaming server (port 8081 by default)
 *    GET /mainstream    MJPEG stream of the camera image
 *    GET /pulsestream   MJPEG stream of the normalized, magnified face
 *    GET /              web page that shows both streams and the latest values
 *
 * Each server accepts connections in its own thread and serves each client in another thread, so the
 * processing loop only stores the latest values and JPEG images (see MainWindow::UpdateProcessFrame).
 * Each JPEG image of the streams carries the number of the processed frame in an X-Frame header.
 * There is no authentication or encryption: listen on other interfaces only in trusted networks.
 *
 * Settings (environment variables, see settingsFromEnvironment):
 *    RT_RPPG_NETWORK=0            disable the network interface
 *    RT_RPPG_BIND=<IPv4 address>  address to listen on; default 127.0.0.1 (this computer only),
 *                                 0.0.0.0 accepts clients from other devices
 *    RT_RPPG_REST_PORT=<port>     default 8080, 0 disables the RESTful API
 *    RT_RPPG_STREAM_PORT=<port>   default 8081, 0 disables the streaming server
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct VitalSigns
{
    float hr = 0;        // heart rate (beats/min)
    float br = 0;        // breathing rate (breaths/min)
    float spo2 = 0;      // oxygen saturation (%), not calibrated
    int seeUser = 0;     // 1 when a user is in front of the camera
    int stable = 0;      // 1 when the values are considered reliable
    float hrGraph = 0;   // latest sample of the pulse signal
    float brGraph = 0;   // latest sample of the breathing signal
};

class network
{

public:

    enum Stream { MAIN_STREAM = 0, PULSE_STREAM = 1 };

    struct Settings
    {
        bool enabled = true;
        std::string bindAddress = "127.0.0.1";
        int restPort = 8080;
        int streamPort = 8081;
    };

    static Settings settingsFromEnvironment();

    network() = default;
    ~network();

    network(const network &) = delete;
    network &operator=(const network &) = delete;

    // Starts the two servers. Returns false if a server could not be started; the other one keeps running.
    bool initNetwork(const Settings &s);

    // Closes all the connections and waits for the client threads to finish.
    void stopNetwork();

    void setVitalSigns(const VitalSigns &values);

    // True while at least one client watches the stream, so the JPEG images are only encoded when needed.
    bool isStreaming(Stream stream) const;

    // Replaces the image of the stream; frame is the number of the processed frame (X-Frame header).
    void sendFrame(Stream stream, std::vector<unsigned char> jpeg, std::uint64_t frame = 0);

private:

    struct Listener
    {
        int fd = -1;
        std::thread thread;
    };

    struct StreamBuffer
    {
        std::mutex m;
        std::condition_variable newFrame;
        std::shared_ptr<const std::vector<unsigned char>> jpeg;
        std::uint64_t frame = 0;
        std::uint64_t count = 0;
        std::atomic<int> clients{0};
    };

    bool startListener(Listener &listener, int port, bool rest);
    void acceptClients(Listener &listener, bool rest);
    bool addClient(int fd);
    void removeClient(int fd);
    void serveClient(int fd, bool rest);
    void serveStream(int fd, Stream stream);

    std::string vitalSignsText();
    std::string vitalSignsJson();
    std::string indexPage() const;

    Settings settings;
    std::atomic<bool> running{false};
    Listener restServer, streamServer;

    std::mutex vitalsMutex;
    VitalSigns vitals;
    std::int64_t vitalsTime = 0;   // time of the last update, ms since the epoch

    StreamBuffer streams[2];

    std::mutex clientsMutex;
    std::condition_variable clientsDone;
    std::set<int> clientSockets;
};

#endif // NETWORK_H

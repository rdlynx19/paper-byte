#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include "../config.h"

// A small AP-mode HTTP file browser for the whole SD card (browse into
// subdirectories, download, delete, upload) — for when the enclosure blocks
// physical access to the card. Meant to be entered deliberately (from the
// Library menu) and torn down again; running WiFi continuously would cost
// real battery for a device that otherwise never wakes the radio.
class FileServer {
public:
    // Starts the AP and HTTP server. Returns false if the AP failed to
    // start (e.g. no memory for the WiFi stack).
    bool begin();

    // Services one pending HTTP request, if any. Call every loop()
    // iteration while active — this is a synchronous server, not
    // interrupt/task driven.
    void update();

    // Tears down the HTTP server and turns the WiFi radio off completely.
    void stop();

    const char *ssid()     const { return WIFI_AP_SSID; }
    const char *password() const { return WIFI_AP_PASSWORD; }
    IPAddress   ip()       const { return WiFi.softAPIP(); }

private:
    WebServer m_server{80};

    void handle_root();       // GET  / ?dir=<path>            — browse a directory
    void handle_download();   // GET  /download?dir=&file=      — stream a file back
    void handle_delete();     // POST /delete  (dir, file)      — remove a file
    void handle_upload_done();
    void handle_upload_data(); // POST /upload?dir=<path>       — add a file
};

extern FileServer g_file_server;

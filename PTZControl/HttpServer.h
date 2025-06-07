#pragma once

#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <map>
#include <mutex>

// Forward declarations
class CPTZControlDlg;

// HTTP request structure
struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::map<std::string, std::string> headers;
};

// HTTP response structure
struct HttpResponse {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
    std::map<std::string, std::string> headers;
};

// HTTP handler function type
typedef std::function<HttpResponse(const HttpRequest&)> HttpHandler;

// Simple HTTP Server class
class CHttpServer
{
public:
    CHttpServer(CPTZControlDlg* pDialog);
    ~CHttpServer();

    // Server control
    bool Start(int port = 5000);
    void Stop();
    bool IsRunning() const { return m_bRunning; }
    int GetPort() const { return m_nPort; }

    // Route registration
    void RegisterRoute(const std::string& method, const std::string& path, HttpHandler handler);

private:
    // Server thread function
    void ServerThread();
    
    // Request handling
    void HandleClient(SOCKET clientSocket);
    HttpRequest ParseRequest(const std::string& requestData);
    std::string BuildResponse(const HttpResponse& response);
    HttpResponse RouteRequest(const HttpRequest& request);
    
    // CORS support
    HttpResponse HandleCORS(const HttpRequest& request);
    
    // Utility functions
    std::string UrlDecode(const std::string& str);
    std::map<std::string, std::string> ParseQueryString(const std::string& query);

private:
    CPTZControlDlg* m_pDialog;
    std::atomic<bool> m_bRunning;
    std::thread m_serverThread;
    SOCKET m_serverSocket;
    int m_nPort;
    
    // Route handling
    std::map<std::string, HttpHandler> m_routes;
    std::mutex m_routesMutex;
    
    // API handlers
    HttpResponse HandleCameraStatus(const HttpRequest& request);
    HttpResponse HandleCameraInfo(const HttpRequest& request);
    HttpResponse HandleCameraPan(const HttpRequest& request);
    HttpResponse HandleCameraTilt(const HttpRequest& request);
    HttpResponse HandleCameraZoom(const HttpRequest& request);
    HttpResponse HandleCameraHome(const HttpRequest& request);
    HttpResponse HandlePresets(const HttpRequest& request);
    HttpResponse HandlePresetSave(const HttpRequest& request);
    HttpResponse HandlePresetRecall(const HttpRequest& request);
    HttpResponse HandleCameraSettings(const HttpRequest& request);
    HttpResponse HandleGetCameraSettings(const HttpRequest& request);
    HttpResponse HandleSetCameraSettings(const HttpRequest& request);
    HttpResponse HandleGetCameraSettingsRanges(const HttpRequest& request);
    HttpResponse HandleResetCameraSettings(const HttpRequest& request);
    HttpResponse HandleAdvancedControls(const HttpRequest& request);
    
    // Helper methods
    std::string JsonEscape(const std::string& str);
    std::string GetCameraStatusJson();
    std::string GetCameraInfoJson();
    std::string GetPresetsJson();
    std::string GetCameraSettingsJson();
};

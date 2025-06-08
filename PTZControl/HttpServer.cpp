#include "pch.h"
#include "HttpServer.h"
#include "PTZControlDlg.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "ws2_32.lib")

CHttpServer::CHttpServer(CPTZControlDlg* pDialog)
    : m_pDialog(pDialog)
    , m_bRunning(false)
    , m_serverSocket(INVALID_SOCKET)
    , m_nPort(5000)
{
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

CHttpServer::~CHttpServer()
{
    Stop();
    WSACleanup();
}

bool CHttpServer::Start(int port)
{
    if (m_bRunning) {
        return false;
    }

    m_nPort = port;

    // Create socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_serverSocket == INVALID_SOCKET) {
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // Bind socket
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(m_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    // Listen for connections
    if (listen(m_serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    // Register API routes
    RegisterRoute("GET", "/api/camera/status", [this](const HttpRequest& req) { return HandleCameraStatus(req); });
    RegisterRoute("GET", "/api/camera/info", [this](const HttpRequest& req) { return HandleCameraInfo(req); });
    RegisterRoute("POST", "/api/camera/pan", [this](const HttpRequest& req) { return HandleCameraPan(req); });
    RegisterRoute("POST", "/api/camera/tilt", [this](const HttpRequest& req) { return HandleCameraTilt(req); });
    RegisterRoute("POST", "/api/camera/zoom", [this](const HttpRequest& req) { return HandleCameraZoom(req); });
    RegisterRoute("POST", "/api/camera/home", [this](const HttpRequest& req) { return HandleCameraHome(req); });
    RegisterRoute("GET", "/api/presets", [this](const HttpRequest& req) { return HandlePresets(req); });
    RegisterRoute("POST", "/api/presets/([0-9]+)/save", [this](const HttpRequest& req) { return HandlePresetSave(req); });
    RegisterRoute("POST", "/api/presets/([0-9]+)/recall", [this](const HttpRequest& req) { return HandlePresetRecall(req); });
    RegisterRoute("GET", "/api/settings", [this](const HttpRequest& req) { return HandleCameraSettings(req); });
    RegisterRoute("POST", "/api/settings", [this](const HttpRequest& req) { return HandleCameraSettings(req); });
    RegisterRoute("GET", "/api/camera/settings/([0-9]+)", [this](const HttpRequest& req) { return HandleGetCameraSettings(req); });
    RegisterRoute("POST", "/api/camera/settings/([0-9]+)", [this](const HttpRequest& req) { return HandleSetCameraSettings(req); });
    RegisterRoute("GET", "/api/camera/settings/([0-9]+)/ranges", [this](const HttpRequest& req) { return HandleGetCameraSettingsRanges(req); });
    RegisterRoute("POST", "/api/camera/settings/([0-9]+)/reset", [this](const HttpRequest& req) { return HandleResetCameraSettings(req); });
    RegisterRoute("GET", "/api/camera/ranges/validation", [this](const HttpRequest& req) { return HandleGetCameraRangesValidation(req); });
    RegisterRoute("POST", "/api/controls/advanced", [this](const HttpRequest& req) { return HandleAdvancedControls(req); });
    RegisterRoute("OPTIONS", ".*", [this](const HttpRequest& req) { return HandleCORS(req); });

    // Start server thread
    m_bRunning = true;
    m_serverThread = std::thread(&CHttpServer::ServerThread, this);

    return true;
}

void CHttpServer::Stop()
{
    if (!m_bRunning) {
        return;
    }

    m_bRunning = false;

    if (m_serverSocket != INVALID_SOCKET) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
    }

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

void CHttpServer::RegisterRoute(const std::string& method, const std::string& path, HttpHandler handler)
{
    std::lock_guard<std::mutex> lock(m_routesMutex);
    std::string key = method + " " + path;
    m_routes[key] = handler;
}

void CHttpServer::ServerThread()
{
    while (m_bRunning) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_serverSocket, &readfds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int result = select(0, &readfds, nullptr, nullptr, &timeout);
        if (result > 0 && FD_ISSET(m_serverSocket, &readfds)) {
            SOCKET clientSocket = accept(m_serverSocket, nullptr, nullptr);
            if (clientSocket != INVALID_SOCKET) {
                std::thread clientThread(&CHttpServer::HandleClient, this, clientSocket);
                clientThread.detach();
            }
        }
    }
}

void CHttpServer::HandleClient(SOCKET clientSocket)
{
    char buffer[4096];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        std::string requestData(buffer);
        
        HttpRequest request = ParseRequest(requestData);
        HttpResponse response = RouteRequest(request);
        
        std::string responseStr = BuildResponse(response);
        send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
    }
    
    closesocket(clientSocket);
}

HttpRequest CHttpServer::ParseRequest(const std::string& requestData)
{
    HttpRequest request;
    std::istringstream stream(requestData);
    std::string line;
    
    // Parse request line
    if (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string pathAndQuery;
        lineStream >> request.method >> pathAndQuery;
        
        // Split path and query
        size_t queryPos = pathAndQuery.find('?');
        if (queryPos != std::string::npos) {
            request.path = pathAndQuery.substr(0, queryPos);
            request.query = pathAndQuery.substr(queryPos + 1);
        } else {
            request.path = pathAndQuery;
        }
    }
    
    // Parse headers
    while (std::getline(stream, line) && line != "\r") {
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r") + 1);
            
            request.headers[key] = value;
        }
    }
    
    // Parse body
    std::string body;
    while (std::getline(stream, line)) {
        body += line + "\n";
    }
    if (!body.empty()) {
        body.pop_back(); // Remove last newline
    }
    request.body = body;
    
    return request;
}

std::string CHttpServer::BuildResponse(const HttpResponse& response)
{
    std::ostringstream stream;
    
    // Status line
    stream << "HTTP/1.1 " << response.status_code << " ";
    switch (response.status_code) {
        case 200: stream << "OK"; break;
        case 400: stream << "Bad Request"; break;
        case 404: stream << "Not Found"; break;
        case 500: stream << "Internal Server Error"; break;
        default: stream << "Unknown"; break;
    }
    stream << "\r\n";
    
    // Headers
    stream << "Content-Type: " << response.content_type << "\r\n";
    stream << "Content-Length: " << response.body.length() << "\r\n";
    stream << "Access-Control-Allow-Origin: *\r\n";
    stream << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    stream << "Access-Control-Allow-Headers: Content-Type\r\n";
    
    for (const auto& header : response.headers) {
        stream << header.first << ": " << header.second << "\r\n";
    }
    
    stream << "\r\n";
    
    // Body
    stream << response.body;
    
    return stream.str();
}

HttpResponse CHttpServer::RouteRequest(const HttpRequest& request)
{
    std::lock_guard<std::mutex> lock(m_routesMutex);
    
    std::string key = request.method + " " + request.path;
    
    // Try exact match first
    auto it = m_routes.find(key);
    if (it != m_routes.end()) {
        return it->second(request);
    }
    
    // Try pattern matching
    for (const auto& route : m_routes) {
        std::string routeKey = route.first;
        size_t spacePos = routeKey.find(' ');
        if (spacePos != std::string::npos) {
            std::string routeMethod = routeKey.substr(0, spacePos);
            std::string routePattern = routeKey.substr(spacePos + 1);
            
            if (routeMethod == request.method || routePattern == ".*") {
                // Simple pattern matching for preset routes
                if (routePattern.find("([0-9]+)") != std::string::npos) {
                    std::string pattern = routePattern;
                    size_t pos = pattern.find("([0-9]+)");
                    pattern.replace(pos, 8, "\\d+");
                    
                    // Extract base path from route pattern for matching
                    std::string basePath = routePattern.substr(0, routePattern.find("([0-9]+)"));
                    
                    // Check if request path matches the base pattern and has a number
                    if (request.path.find(basePath) == 0) {
                        // Extract the part after base path to validate it's a number
                        std::string remainder = request.path.substr(basePath.length());
                        size_t slashPos = remainder.find('/');
                        std::string numberPart = (slashPos != std::string::npos) ? remainder.substr(0, slashPos) : remainder;
                        
                        // Simple check if it's all digits
                        bool isNumber = !numberPart.empty() && std::all_of(numberPart.begin(), numberPart.end(), ::isdigit);
                        if (isNumber) {
                            return route.second(request);
                        }
                    }
                } else if (routePattern == ".*") {
                    return route.second(request);
                }
            }
        }
    }
    
    // Not found
    HttpResponse response;
    response.status_code = 404;
    response.body = "{\"error\":\"Not found\"}";
    return response;
}

HttpResponse CHttpServer::HandleGetCameraRangesValidation(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            
            std::ostringstream json;
            json << "{";
            json << "\"validation\":{";
            
            // VideoProcAmp validation
            json << "\"videoProcAmp\":{";
            const struct { int prop; const char* name; int expectedMin; int expectedMax; int expectedDefault; } vpProps[] = {
                {VideoProcAmp_Brightness, "brightness", 0, 255, 128},
                {VideoProcAmp_Contrast, "contrast", 0, 255, 128},
                {VideoProcAmp_Saturation, "saturation", 0, 255, 128},
                {VideoProcAmp_Sharpness, "sharpness", 0, 255, 128},
                {VideoProcAmp_WhiteBalance, "whiteBalance", 2800, 6500, 4600},
                {VideoProcAmp_BacklightCompensation, "backlightCompensation", 0, 1, 0},
                {VideoProcAmp_Gain, "gain", 0, 32, 0}
            };
            
            bool first = true;
            for (const auto& prop : vpProps) {
                if (!first) json << ",";
                first = false;
                
                long min, max, step, def, flags;
                HRESULT hr = cam.TestAndGetVideoProcAmpRange(prop.prop, &min, &max, &step, &def, &flags);
                
                json << "\"" << prop.name << "\":{";
                if (SUCCEEDED(hr) && flags != 0) {
                    json << "\"supported\":true,";
                    json << "\"actual\":{\"min\":" << min << ",\"max\":" << max << ",\"default\":" << def << "},";
                    json << "\"expected\":{\"min\":" << prop.expectedMin << ",\"max\":" << prop.expectedMax << ",\"default\":" << prop.expectedDefault << "},";
                    json << "\"matches\":" << ((min == prop.expectedMin && max == prop.expectedMax && def == prop.expectedDefault) ? "true" : "false") << ",";
                    json << "\"autoSupported\":" << ((flags & VideoProcAmp_Flags_Auto) ? "true" : "false") << ",";
                    json << "\"manualSupported\":" << ((flags & VideoProcAmp_Flags_Manual) ? "true" : "false");
                } else {
                    json << "\"supported\":false,\"error\":\"0x" << std::hex << hr << "\"";
                }
                json << "}";
            }
            json << "},";
            
            // CameraControl validation
            json << "\"cameraControl\":{";
            const struct { int prop; const char* name; int expectedMin; int expectedMax; int expectedDefault; } ccProps[] = {
                {CameraControl_Focus, "focus", 0, 255, 8},
                {CameraControl_Exposure, "exposure", -11, -2, -6},
                {CameraControl_Zoom, "zoom", 100, 500, 100}
            };
            
            first = true;
            for (const auto& prop : ccProps) {
                if (!first) json << ",";
                first = false;
                
                long min, max, step, def, flags;
                HRESULT hr = cam.TestAndGetCameraControlRange(prop.prop, &min, &max, &step, &def, &flags);
                
                json << "\"" << prop.name << "\":{";
                if (SUCCEEDED(hr) && flags != 0) {
                    json << "\"supported\":true,";
                    json << "\"actual\":{\"min\":" << min << ",\"max\":" << max << ",\"default\":" << def << "},";
                    json << "\"expected\":{\"min\":" << prop.expectedMin << ",\"max\":" << prop.expectedMax << ",\"default\":" << prop.expectedDefault << "},";
                    json << "\"matches\":" << ((min == prop.expectedMin && max == prop.expectedMax && def == prop.expectedDefault) ? "true" : "false") << ",";
                    json << "\"autoSupported\":" << ((flags & CameraControl_Flags_Auto) ? "true" : "false") << ",";
                    json << "\"manualSupported\":" << ((flags & CameraControl_Flags_Manual) ? "true" : "false");
                } else {
                    json << "\"supported\":false,\"error\":\"0x" << std::hex << hr << "\"";
                }
                json << "}";
            }
            json << "}";
            
            json << "}";
            json << "}";
            
            response.body = json.str();
        } else {
            response.status_code = 500;
            response.body = "{\"error\":\"Camera not available\"}";
        }
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to validate camera ranges\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleCORS(const HttpRequest& request)
{
    HttpResponse response;
    response.status_code = 200;
    response.body = "";
    return response;
}

HttpResponse CHttpServer::HandleCameraStatus(const HttpRequest& request)
{
    HttpResponse response;
    response.body = GetCameraStatusJson();
    return response;
}

HttpResponse CHttpServer::HandleCameraInfo(const HttpRequest& request)
{
    HttpResponse response;
    response.body = GetCameraInfoJson();
    return response;
}

HttpResponse CHttpServer::HandleCameraPan(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        // Parse JSON body to get direction
        int direction = 0;
        if (request.body.find("\"direction\":-1") != std::string::npos) {
            direction = -1;
        } else if (request.body.find("\"direction\":1") != std::string::npos) {
            direction = 1;
        }
        
        // Call the camera control method
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            if (direction == 0) {
                // Stop pan movement - this might need adjustment based on implementation
            } else {
                cam.MovePan(direction);
            }
        }
        
        response.body = "{\"success\":true}";
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to control pan\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleCameraTilt(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        // Parse JSON body to get direction
        int direction = 0;
        if (request.body.find("\"direction\":-1") != std::string::npos) {
            direction = -1;
        } else if (request.body.find("\"direction\":1") != std::string::npos) {
            direction = 1;
        }
        
        // Call the camera control method
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            if (direction == 0) {
                // Stop tilt movement
            } else {
                cam.MoveTilt(direction);
            }
        }
        
        response.body = "{\"success\":true}";
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to control tilt\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleCameraZoom(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        // Parse JSON body to get direction
        int direction = 0;
        if (request.body.find("\"direction\":-1") != std::string::npos) {
            direction = -1;
        } else if (request.body.find("\"direction\":1") != std::string::npos) {
            direction = 1;
        }
        
        // Call the camera control method
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            cam.Zoom(direction);
        }
        
        response.body = "{\"success\":true}";
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to control zoom\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleCameraHome(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        // Call the camera control method
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            cam.GotoHome();
        }
        
        response.body = "{\"success\":true}";
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to go to home position\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandlePresets(const HttpRequest& request)
{
    HttpResponse response;
    response.body = GetPresetsJson();
    return response;
}

HttpResponse CHttpServer::HandlePresetSave(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        // Extract preset ID from path
        size_t pos = request.path.find("/api/presets/");
        if (pos != std::string::npos) {
            pos += 13; // Length of "/api/presets/"
            size_t endPos = request.path.find("/", pos);
            if (endPos != std::string::npos) {
                std::string presetIdStr = request.path.substr(pos, endPos - pos);
                int presetId = std::stoi(presetIdStr);
                
                if (presetId >= 1 && presetId <= 8) {
                    // Call the camera control method
                    if (m_pDialog) {
                        CWebcamController& cam = m_pDialog->GetCurrentWebCam();
                        cam.SavePreset(presetId - 1); // Convert to 0-based index
                    }
                    
                    response.body = "{\"success\":true}";
                } else {
                    response.status_code = 400;
                    response.body = "{\"error\":\"Invalid preset ID\"}";
                }
            }
        }
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to save preset\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandlePresetRecall(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        // Extract preset ID from path
        size_t pos = request.path.find("/api/presets/");
        if (pos != std::string::npos) {
            pos += 13; // Length of "/api/presets/"
            size_t endPos = request.path.find("/", pos);
            if (endPos != std::string::npos) {
                std::string presetIdStr = request.path.substr(pos, endPos - pos);
                int presetId = std::stoi(presetIdStr);
                
                if (presetId >= 1 && presetId <= 8) {
                    // Call the camera control method
                    if (m_pDialog) {
                        CWebcamController& cam = m_pDialog->GetCurrentWebCam();
                        cam.GotoPreset(presetId - 1); // Convert to 0-based index
                    }
                    
                    response.body = "{\"success\":true}";
                } else {
                    response.status_code = 400;
                    response.body = "{\"error\":\"Invalid preset ID\"}";
                }
            }
        }
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to recall preset\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleCameraSettings(const HttpRequest& request)
{
    HttpResponse response;
    
    if (request.method == "GET") {
        response.body = GetCameraSettingsJson();
    } else if (request.method == "POST") {
        try {
            std::string body = request.body;
            
            if (m_pDialog) {
                CWebcamController& cam = m_pDialog->GetCurrentWebCam();
                
                // Parse and apply DirectShow properties properly using hybrid methods
                if (body.find("\"brightness\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "brightness");
                    cam.SetPropertyHybrid(VideoProcAmp_Brightness, value, true);
                }
                
                if (body.find("\"contrast\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "contrast");
                    cam.SetPropertyHybrid(VideoProcAmp_Contrast, value, true);
                }
                
                if (body.find("\"saturation\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "saturation");
                    cam.SetPropertyHybrid(VideoProcAmp_Saturation, value, true);
                }
                
                if (body.find("\"hue\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "hue");
                    cam.SetPropertyHybrid(VideoProcAmp_Hue, value, true);
                }
                
                if (body.find("\"sharpness\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "sharpness");
                    cam.SetPropertyHybrid(VideoProcAmp_Sharpness, value, true);
                }
                
                if (body.find("\"gamma\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "gamma");
                    cam.SetPropertyHybrid(VideoProcAmp_Gamma, value, true);
                }
                
                if (body.find("\"whiteBalance\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "whiteBalance");
                    cam.SetPropertyHybrid(VideoProcAmp_WhiteBalance, value, true);
                }
                
                if (body.find("\"backlightCompensation\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "backlightCompensation");
                    cam.SetPropertyHybrid(VideoProcAmp_BacklightCompensation, value, true);
                }
                
                if (body.find("\"gain\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "gain");
                    cam.SetPropertyHybrid(VideoProcAmp_Gain, value, true);
                }
                
                if (body.find("\"exposure\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "exposure");
                    cam.SetPropertyHybrid(CameraControl_Exposure, value, false);
                }
                
                if (body.find("\"focus\"") != std::string::npos) {
                    long value = ExtractLongValue(body, "focus");
                    cam.SetPropertyHybrid(CameraControl_Focus, value, false);
                }
                
                // Note: PowerlineFrequency is not a standard DirectShow property
                // It would need to be handled through extension units if supported
            }
            
            response.body = "{\"success\":true}";
        } catch (...) {
            response.status_code = 500;
            response.body = "{\"error\":\"Failed to update camera settings\"}";
        }
    }
    
    return response;
}

HttpResponse CHttpServer::HandleAdvancedControls(const HttpRequest& request)
{
    HttpResponse response;
    response.body = "{\"success\":true}";
    return response;
}

// Helper function to extract long values from JSON
long CHttpServer::ExtractLongValue(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos != std::string::npos) {
        size_t valueStart = pos + searchKey.length();
        size_t valueEnd = json.find_first_of(",}", valueStart);
        if (valueEnd != std::string::npos) {
            std::string valueStr = json.substr(valueStart, valueEnd - valueStart);
            // Remove quotes if present
            if (valueStr.front() == '"' && valueStr.back() == '"') {
                valueStr = valueStr.substr(1, valueStr.length() - 2);
            }
            return std::stol(valueStr);
        }
    }
    return 0;
}

std::string CHttpServer::JsonEscape(const std::string& str)
{
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

std::string CHttpServer::GetCameraStatusJson()
{
    std::ostringstream json;
    json << "{";
    json << "\"connected\":true,";
    json << "\"pan\":0,";
    json << "\"tilt\":0,";
    json << "\"zoom\":100";
    json << "}";
    return json.str();
}

std::string CHttpServer::GetCameraInfoJson()
{
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"Logitech Rally Camera\",";
    json << "\"version\":\"1.0\",";
    json << "\"capabilities\":[\"pan\",\"tilt\",\"zoom\",\"presets\"]";
    json << "}";
    return json.str();
}

std::string CHttpServer::GetPresetsJson()
{
    std::ostringstream json;
    json << "[";
    for (int i = 1; i <= 8; i++) {
        if (i > 1) json << ",";
        json << "{";
        json << "\"id\":" << i << ",";
        json << "\"name\":\"Preset " << i << "\"";
        json << "}";
    }
    json << "]";
    return json.str();
}

std::string CHttpServer::GetCameraSettingsJson()
{
    std::ostringstream json;
    json << "{";
    json << "\"model\":\"Logitech Rally Camera\",";
    json << "\"exposure\":\"AUTO\",";
    json << "\"whiteBalance\":\"AUTO\",";
    json << "\"gain\":0,";
    json << "\"rightLight\":false,";
    json << "\"autoFocus\":true";
    json << "}";
    return json.str();
}

std::string CHttpServer::UrlDecode(const std::string& str)
{
    std::string decoded;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                decoded += static_cast<char>(value);
                i += 2;
            } else {
                decoded += str[i];
            }
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }
    return decoded;
}

std::map<std::string, std::string> CHttpServer::ParseQueryString(const std::string& query)
{
    std::map<std::string, std::string> params;
    std::istringstream stream(query);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = UrlDecode(pair.substr(0, pos));
            std::string value = UrlDecode(pair.substr(pos + 1));
            params[key] = value;
        }
    }
    
    return params;
}

HttpResponse CHttpServer::HandleGetCameraSettings(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            CWebcamController::CameraSettings settings = cam.GetAllCameraSettings();
            
            std::ostringstream json;
            json << "{";
            json << "\"brightness\":" << settings.brightness << ",";
            json << "\"contrast\":" << settings.contrast << ",";
            json << "\"hue\":" << settings.hue << ",";
            json << "\"saturation\":" << settings.saturation << ",";
            json << "\"sharpness\":" << settings.sharpness << ",";
            json << "\"gamma\":" << settings.gamma << ",";
            json << "\"whiteBalance\":" << settings.whiteBalance << ",";
            json << "\"backlightCompensation\":" << settings.backlightCompensation << ",";
            json << "\"gain\":" << settings.gain << ",";
            json << "\"colorEnable\":" << settings.colorEnable << ",";
            json << "\"powerlineFrequency\":" << settings.powerlineFrequency << ",";
            json << "\"exposure\":" << settings.exposure << ",";
            json << "\"focus\":" << settings.focus << ",";
            json << "\"autoExposure\":" << (settings.autoExposure ? "true" : "false") << ",";
            json << "\"autoWhiteBalance\":" << (settings.autoWhiteBalance ? "true" : "false") << ",";
            json << "\"autoFocus\":" << (settings.autoFocus ? "true" : "false") << ",";
            json << "\"rightLight\":" << (settings.rightLight ? "true" : "false");
            json << "}";
            
            response.body = json.str();
        } else {
            response.status_code = 500;
            response.body = "{\"error\":\"Camera not available\"}";
        }
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to get camera settings\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleSetCameraSettings(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            std::string body = request.body;
            
            HRESULT overallResult = S_OK;
            std::string errorDetails;
            
            // Parse and apply each setting individually with error checking
            if (body.find("\"brightness\"") != std::string::npos) {
                long value = ExtractLongValue(body, "brightness");
                HRESULT hr = cam.SetBrightness(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "brightness failed; ";
                }
            }
            
            if (body.find("\"contrast\"") != std::string::npos) {
                long value = ExtractLongValue(body, "contrast");
                HRESULT hr = cam.SetContrast(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "contrast failed; ";
                }
            }
            
            if (body.find("\"saturation\"") != std::string::npos) {
                long value = ExtractLongValue(body, "saturation");
                HRESULT hr = cam.SetSaturation(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "saturation failed; ";
                }
            }
            
            if (body.find("\"hue\"") != std::string::npos) {
                long value = ExtractLongValue(body, "hue");
                HRESULT hr = cam.SetHue(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "hue failed; ";
                }
            }
            
            if (body.find("\"sharpness\"") != std::string::npos) {
                long value = ExtractLongValue(body, "sharpness");
                HRESULT hr = cam.SetSharpness(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "sharpness failed; ";
                }
            }
            
            if (body.find("\"gamma\"") != std::string::npos) {
                long value = ExtractLongValue(body, "gamma");
                HRESULT hr = cam.SetGamma(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "gamma failed; ";
                }
            }
            
            if (body.find("\"gain\"") != std::string::npos) {
                long value = ExtractLongValue(body, "gain");
                HRESULT hr = cam.SetGain(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "gain failed; ";
                }
            }
            
            if (body.find("\"backlightCompensation\"") != std::string::npos) {
                long value = ExtractLongValue(body, "backlightCompensation");
                HRESULT hr = cam.SetBacklightCompensation(value);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "backlightCompensation failed; ";
                }
            }
            
            if (body.find("\"whiteBalance\"") != std::string::npos) {
                long value = ExtractLongValue(body, "whiteBalance");
                bool isAuto = body.find("\"autoWhiteBalance\":true") != std::string::npos;
                HRESULT hr = cam.SetWhiteBalance(value, isAuto);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "whiteBalance failed; ";
                }
            }
            
            if (body.find("\"autoWhiteBalance\"") != std::string::npos) {
                bool isAuto = body.find("\"autoWhiteBalance\":true") != std::string::npos;
                long currentValue = 5200; // Default
                if (body.find("\"whiteBalance\"") != std::string::npos) {
                    currentValue = ExtractLongValue(body, "whiteBalance");
                }
                HRESULT hr = cam.SetWhiteBalance(currentValue, isAuto);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "autoWhiteBalance failed; ";
                }
            }
            
            if (body.find("\"exposure\"") != std::string::npos) {
                long value = ExtractLongValue(body, "exposure");
                bool isAuto = body.find("\"autoExposure\":true") != std::string::npos;
                HRESULT hr = cam.SetExposure(value, isAuto);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "exposure failed; ";
                }
            }
            
            if (body.find("\"autoExposure\"") != std::string::npos) {
                bool isAuto = body.find("\"autoExposure\":true") != std::string::npos;
                long currentValue = -5; // Default
                if (body.find("\"exposure\"") != std::string::npos) {
                    currentValue = ExtractLongValue(body, "exposure");
                }
                HRESULT hr = cam.SetExposure(currentValue, isAuto);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "autoExposure failed; ";
                }
            }
            
            if (body.find("\"focus\"") != std::string::npos) {
                long value = ExtractLongValue(body, "focus");
                bool isAuto = body.find("\"autoFocus\":true") != std::string::npos;
                HRESULT hr = cam.SetFocus(value, isAuto);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "focus failed; ";
                }
            }
            
            if (body.find("\"autoFocus\"") != std::string::npos) {
                bool isAuto = body.find("\"autoFocus\":true") != std::string::npos;
                long currentValue = 50; // Default
                if (body.find("\"focus\"") != std::string::npos) {
                    currentValue = ExtractLongValue(body, "focus");
                }
                HRESULT hr = cam.SetFocus(currentValue, isAuto);
                if (FAILED(hr)) {
                    overallResult = hr;
                    errorDetails += "autoFocus failed; ";
                }
            }
            
            // Create detailed response
            if (SUCCEEDED(overallResult)) {
                response.body = "{\"success\":true}";
            } else {
                response.status_code = 500;
                response.body = "{\"success\":false,\"error\":\"" + errorDetails + "\",\"hresult\":\"0x" + 
                    std::to_string(static_cast<unsigned long>(overallResult)) + "\"}";
            }
        } else {
            response.status_code = 500;
            response.body = "{\"error\":\"Camera not available\"}";
        }
    } catch (const std::exception& e) {
        response.status_code = 500;
        response.body = "{\"error\":\"Exception: " + std::string(e.what()) + "\"}";
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Unknown error occurred\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleGetCameraSettingsRanges(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            
            std::map<long, CWebcamController::PropertyRange> videoProcAmpRanges;
            std::map<long, CWebcamController::PropertyRange> cameraControlRanges;
            
            cam.GetPropertyRanges(videoProcAmpRanges, cameraControlRanges);
            
            std::ostringstream json;
            json << "{";
            json << "\"videoProcAmp\":{";
            
            bool first = true;
            for (const auto& pair : videoProcAmpRanges) {
                if (!first) json << ",";
                first = false;
                
                std::string propertyName;
                switch (pair.first) {
                    case VideoProcAmp_Brightness: propertyName = "brightness"; break;
                    case VideoProcAmp_Contrast: propertyName = "contrast"; break;
                    case VideoProcAmp_Hue: propertyName = "hue"; break;
                    case VideoProcAmp_Saturation: propertyName = "saturation"; break;
                    case VideoProcAmp_Sharpness: propertyName = "sharpness"; break;
                    case VideoProcAmp_Gamma: propertyName = "gamma"; break;
                    case VideoProcAmp_WhiteBalance: propertyName = "whiteBalance"; break;
                    case VideoProcAmp_BacklightCompensation: propertyName = "backlightCompensation"; break;
                    case VideoProcAmp_Gain: propertyName = "gain"; break;
                    case VideoProcAmp_ColorEnable: propertyName = "colorEnable"; break;
                    default: continue;
                }
                
                const auto& range = pair.second;
                json << "\"" << propertyName << "\":{";
                json << "\"min\":" << range.min << ",";
                json << "\"max\":" << range.max << ",";
                json << "\"step\":" << range.step << ",";
                json << "\"default\":" << range.defaultValue << ",";
                json << "\"flags\":" << range.flags;
                json << "}";
            }
            
            json << "},";
            json << "\"cameraControl\":{";
            
            first = true;
            for (const auto& pair : cameraControlRanges) {
                if (!first) json << ",";
                first = false;
                
                std::string propertyName;
                switch (pair.first) {
                    case CameraControl_Exposure: propertyName = "exposure"; break;
                    case CameraControl_Focus: propertyName = "focus"; break;
                    case CameraControl_Zoom: propertyName = "zoom"; break;
                    case CameraControl_Pan: propertyName = "pan"; break;
                    case CameraControl_Tilt: propertyName = "tilt"; break;
                    default: continue;
                }
                
                const auto& range = pair.second;
                json << "\"" << propertyName << "\":{";
                json << "\"min\":" << range.min << ",";
                json << "\"max\":" << range.max << ",";
                json << "\"step\":" << range.step << ",";
                json << "\"default\":" << range.defaultValue << ",";
                json << "\"flags\":" << range.flags;
                json << "}";
            }
            
            json << "}";
            json << "}";
            
            response.body = json.str();
        } else {
            response.status_code = 500;
            response.body = "{\"error\":\"Camera not available\"}";
        }
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to get camera settings ranges\"}";
    }
    
    return response;
}

HttpResponse CHttpServer::HandleResetCameraSettings(const HttpRequest& request)
{
    HttpResponse response;
    
    try {
        if (m_pDialog) {
            CWebcamController& cam = m_pDialog->GetCurrentWebCam();
            
            if (SUCCEEDED(cam.ResetCameraSettings())) {
                response.body = "{\"success\":true}";
            } else {
                response.status_code = 500;
                response.body = "{\"error\":\"Failed to reset camera settings\"}";
            }
        } else {
            response.status_code = 500;
            response.body = "{\"error\":\"Camera not available\"}";
        }
    } catch (...) {
        response.status_code = 500;
        response.body = "{\"error\":\"Failed to reset camera settings\"}";
    }
    
    return response;
}

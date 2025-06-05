# PTZControl HTTP Server Integration

This document describes the HTTP server integration added to PTZControl for remote camera control via web dashboard.

## Overview

The PTZControl application has been extended with an HTTP server that exposes camera control functionality through REST API endpoints. This allows remote control of PTZ cameras from a web dashboard running on a separate computer.

## Architecture

### Modified Files

1. **HttpServer.h** - HTTP server class definition
2. **HttpServer.cpp** - HTTP server implementation with REST API endpoints
3. **PTZControlDlg.h** - Added HTTP server member variable
4. **PTZControlDlg.cpp** - Integrated HTTP server initialization and cleanup
5. **PTZControl.vcxproj** - Added new files to Visual Studio project

### Key Features

- **HTTP Server**: Lightweight HTTP server running on port 5000
- **REST API**: RESTful endpoints for camera control
- **CORS Support**: Cross-origin resource sharing for web dashboard access
- **Thread Safety**: Multi-threaded request handling with proper synchronization
- **Error Handling**: Graceful error handling and recovery

## API Endpoints

### Camera Control
- `GET /api/camera/status` - Get camera status (pan, tilt, zoom, connection)
- `GET /api/camera/info` - Get camera information and capabilities
- `POST /api/camera/pan` - Control camera pan movement
- `POST /api/camera/tilt` - Control camera tilt movement
- `POST /api/camera/zoom` - Control camera zoom
- `POST /api/camera/home` - Move camera to home position

### Preset Management
- `GET /api/presets` - List all saved presets
- `POST /api/presets/{id}/save` - Save current position to preset
- `POST /api/presets/{id}/recall` - Recall saved preset

### Camera Settings
- `GET /api/settings` - Get current camera settings
- `POST /api/settings` - Update camera settings
- `POST /api/controls/advanced` - Advanced camera controls

## Usage

### Building the Application

1. Open PTZControl.sln in Visual Studio 2019 or later
2. Build the solution (Debug or Release configuration)
3. The HTTP server will automatically start when the application launches

### Testing the HTTP Server

1. Run PTZControl.exe
2. The HTTP server starts automatically on port 5000
3. Test endpoints using curl or a web browser:

```bash
# Test camera status
curl http://localhost:5000/api/camera/status

# Test camera info
curl http://localhost:5000/api/camera/info

# Test pan control
curl -X POST http://localhost:5000/api/camera/pan -H "Content-Type: application/json" -d '{"direction":1}'

# Test home position
curl -X POST http://localhost:5000/api/camera/home
```

### Web Dashboard Integration

The web dashboard (located in the Dashboard folder) is designed to work with this HTTP server:

1. Each NUC computer runs PTZControl with HTTP server enabled
2. The central recording computer runs the web dashboard
3. Dashboard communicates with all 5 NUCs via HTTP requests
4. Real-time camera control from centralized interface

## Network Configuration

### Default Settings
- **Port**: 5000
- **Interface**: All interfaces (0.0.0.0)
- **Protocol**: HTTP (not HTTPS)

### Firewall Configuration
Ensure Windows Firewall allows incoming connections on port 5000:

```cmd
netsh advfirewall firewall add rule name="PTZControl HTTP Server" dir=in action=allow protocol=TCP localport=5000
```

### Network Discovery
The system expects cameras at these IP addresses:
- Camera 1: 192.168.50.2:5000
- Camera 2: 192.168.50.3:5000
- Camera 3: 192.168.50.4:5000
- Camera 4: 192.168.50.5:5000
- Camera 5: 192.168.50.6:5000

## Troubleshooting

### Common Issues

1. **HTTP Server Won't Start**
   - Check if port 5000 is already in use
   - Verify Windows Firewall settings
   - Run as Administrator if needed

2. **Camera Control Not Working**
   - Ensure camera is properly connected via USB
   - Check that PTZControl can control camera locally
   - Verify API request format (JSON content-type)

3. **Network Connectivity Issues**
   - Test with curl or browser first
   - Check network configuration and routing
   - Verify all computers are on same subnet

### Logging

The application logs HTTP server events to the console. For debugging:
1. Run PTZControl from command line to see console output
2. Check for HTTP server startup messages
3. Monitor for request/response logging

## Security Considerations

- **No Authentication**: Current implementation has no authentication
- **Local Network Only**: Designed for private network use
- **HTTP Only**: No HTTPS encryption (suitable for local network)

For production use, consider adding:
- Basic authentication
- HTTPS support
- Request rate limiting
- Input validation

## Future Enhancements

Planned improvements:
1. **Camera Settings API**: Full exposure, white balance, focus control
2. **WebSocket Support**: Real-time status updates
3. **Configuration API**: Network settings and camera configuration
4. **Authentication**: Basic auth or API key support
5. **Service Mode**: Run as Windows service

## Development Notes

### Code Structure
- `CHttpServer` class handles all HTTP functionality
- Request routing uses simple pattern matching
- Camera operations call existing `CWebcamController` methods
- Thread-safe design with mutex protection

### Dependencies
- Windows Sockets (ws2_32.lib)
- Standard C++ threading library
- MFC framework for dialog integration

### Testing
- Unit tests can be added for HTTP server functionality
- Integration tests with actual camera hardware
- Load testing for multiple concurrent requests

#include "painlessMesh.h"

class MeshEngine {
private:
    painlessMesh mesh;
    Scheduler userScheduler; // Required by painlessMesh to handle internal tasks safely
    
public:
    void begin() {
        // Initialize the mesh network on a hidden, localized SSID
        mesh.setDebugMsgTypes(ERROR | STARTUP);
        mesh.init("AC_MESH_NET", "MeshSecurePassword123", &userScheduler, 5555);
        
        // Register callbacks
        mesh.onReceive([](uint32_t from, String &msg) {
            // Handle incoming data packet (parse JSON)
        });
        
        mesh.onNewConnection([](uint32_t nodeId) {
            log_i("New node joined the mesh! ID: %u", nodeId);
        });
    }

    void update() {
        mesh.update(); // Must be called frequently in your execution loop
    }

    void broadcastTelemetry(String jsonPayload) {
        mesh.sendBroadcast(jsonPayload);
    }
};

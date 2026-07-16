#ifndef MESH_ENGINE_H
#define MESH_ENGINE_H

#include <Arduino.h>
#include "painlessMesh.h"

class MeshEngine {
private:
    painlessMesh mesh;
    Scheduler userScheduler; // painlessMesh requires this to run its internal tasks safely

public:
    // Constructor
    MeshEngine() {}

    // Initialize the mesh network
    void begin() {
        // Log levels: only show errors and startup events to keep the console clean
        mesh.setDebugMsgTypes(ERROR | STARTUP);

        // Initialize the mesh:
        // "AC_MESH_NET" = The hidden Wi-Fi network the nodes use to talk to each other
        // "MeshSecurePassword123" = The password to join the mesh
        // 5555 = The port used for wireless communication
        mesh.init("AC_MESH_NET", "MeshSecurePassword123", &userScheduler, 5555);

        // Event: What to do when another node joins the mesh
        mesh.onNewConnection([](uint32_t nodeId) {
            log_i("New node joined the mesh! Assigned ID: %u", nodeId);
        });
    }

    // This must be called repeatedly in your main loop to keep the mesh alive
    void update() {
        mesh.update();
    }

    // Send data to every node currently in the mesh
    void broadcast(const String &jsonPayload) {
        mesh.sendBroadcast(jsonPayload);
    }
};

#endif // MESH_ENGINE_H

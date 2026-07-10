#ifndef COMM_MANAGER_H
#define COMM_MANAGER_H

#include <Arduino.h>

class CommManager {
public:
    static void begin();
    static void process();
    static void sendCommand(const String& cmd);

private:
    static void dispatchJson(const String& line);
    static String uartBuf;
};

#endif // COMM_MANAGER_H

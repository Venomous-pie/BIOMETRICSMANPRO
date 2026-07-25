#include "audio_manager.h"
#include "config.h"
#include <DFRobotDFPlayerMini.h>

HardwareSerial dfpSerial(2); // Use UART2 for DFPlayer
DFRobotDFPlayerMini myDFPlayer;

void audioManagerInit() {
  // Initialize Buzzer
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // Initialize DFPlayer Mini on UART2
  dfpSerial.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  
  Serial.println(F("[AUDIO] Initializing DFPlayer Mini..."));
  
  // Try to initialize, wait a bit for it to boot
  if (!myDFPlayer.begin(dfpSerial)) {
    Serial.println(F("[AUDIO] Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    // Not blocking, just won't play audio
  } else {
    Serial.println(F("[AUDIO] DFPlayer Mini online."));
    myDFPlayer.setTimeOut(500); // Set serial communication time out 500ms
    myDFPlayer.volume(25);      // Set volume value (0~30).
    myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
  }
}

void playTrack(int trackNum) {
  Serial.printf("[AUDIO] Playing MP3 track %d\n", trackNum);
  myDFPlayer.playMp3Folder(trackNum); // Plays files in the /mp3/ folder named 0001.mp3, 0002.mp3...
}

void beep(int durationMs) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(durationMs);
  digitalWrite(PIN_BUZZER, LOW);
}

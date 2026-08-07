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
  // Pass false for isACK and doReset to prevent the library from hanging on noisy RX lines
  if (!myDFPlayer.begin(dfpSerial, false, false)) {
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
  // Uses physical FAT index to play files from the root directory instantly
  myDFPlayer.play(trackNum);
}

void beep(int durationMs) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(durationMs);
  digitalWrite(PIN_BUZZER, LOW);
}

void setVolume(int vol) {
  if (vol < 0) vol = 0;
  if (vol > 30) vol = 30;
  Serial.printf("[AUDIO] Setting volume to %d\n", vol);
  myDFPlayer.volume(vol);
}

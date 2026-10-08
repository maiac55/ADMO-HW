#include <ESP32Servo.h>

namespace
{
  constexpr int kServoPin = 13;
  constexpr int kButtonPin = 27;
  constexpr int kLedPin = 25;
  constexpr int kBuzzerPin = 32;
  constexpr int kStopPulseUs = 1500;
  constexpr int kClockwisePulseUs = 1600;
  constexpr unsigned long kBurstDurationMs = 250;
  constexpr unsigned long kDebounceMs = 40;
  constexpr unsigned long kAlertLedToggleMs = 300;
  constexpr unsigned long kButtonCooldownMs = 3000;

  Servo servo;
  int lastButtonReading = HIGH;
  int buttonState = HIGH;
  unsigned long lastDebounceTime = 0;
  unsigned long burstEndsAt = 0;
  unsigned long nextLedToggleAt = 0;
  bool burstActive = false;
  bool alertActive = false;
  bool buttonCoolingDown = false;
  unsigned long buttonReadyAt = 0;
  bool ledOn = false;
  String serialLine;

  bool updateCooldown(unsigned long now)
  {
    if (buttonCoolingDown && static_cast<long>(now - buttonReadyAt) >= 0)
    {
      buttonCoolingDown = false;
    }
    return buttonCoolingDown;
  }

  void stopAlert()
  {
    alertActive = false;
    ledOn = false;
    digitalWrite(kLedPin, LOW);
    digitalWrite(kBuzzerPin, LOW);
  }

  void startAlert()
  {
    const unsigned long now = millis();

    if (updateCooldown(now))
    {
      const unsigned long secondsLeft = (buttonReadyAt - now + 999) / 1000;
      Serial.print("Button is cooling down. Wait ");
      Serial.print(secondsLeft);
      Serial.println(" seconds before starting another alert.");
      return;
    }

    if (alertActive)
    {
      Serial.println("Alert already active.");
      return;
    }

    alertActive = true;
    ledOn = true;
    digitalWrite(kLedPin, HIGH);
    digitalWrite(kBuzzerPin, HIGH);
    nextLedToggleAt = now + kAlertLedToggleMs;
    Serial.println("Alert active: LED is blinking and buzzer is sounding until the button is pressed.");
  }

  void handleSerial()
  {
    while (Serial.available())
    {
      const char incoming = static_cast<char>(Serial.read());
      if (incoming == '\r' || incoming == '\n')
      {
        serialLine.trim();
        serialLine.toLowerCase();
        if (serialLine == "alert")
        {
          startAlert();
        }
        else if (serialLine == "?" || serialLine == "help")
        {
          Serial.println("Send alert to blink the LED and sound the buzzer. Press GPIO27 to acknowledge.");
        }
        else if (serialLine.length() > 0)
        {
          Serial.println("Unknown command. Send alert or ?.");
        }
        serialLine = "";
      }
      else if (serialLine.length() < 24)
      {
        serialLine += incoming;
      }
      else
      {
        serialLine = "";
        Serial.println("Input too long; command discarded.");
      }
    }
  }

  void handleButton()
  {
    const int reading = digitalRead(kButtonPin);
    if (reading != lastButtonReading)
    {
      lastDebounceTime = millis();
      lastButtonReading = reading;
    }

    if (millis() - lastDebounceTime >= kDebounceMs && reading != buttonState)
    {
      buttonState = reading;
      if (buttonState == LOW)
      {
        if (updateCooldown(millis()))
        {
          Serial.println("Button ignored: 3-second cooldown is active.");
        }
        else if (!alertActive)
        {
          Serial.println("No active alert. Send alert before pressing the button.");
        }
        else
        {
          stopAlert();
          buttonCoolingDown = true;
          buttonReadyAt = millis() + kButtonCooldownMs;
          servo.writeMicroseconds(kClockwisePulseUs);
          burstActive = true;
          burstEndsAt = millis() + kBurstDurationMs;
          Serial.println("Alert acknowledged. One clockwise burst started; button locked for 3 seconds.");
        }
      }
    }
  }

  void updateBurst()
  {
    const unsigned long now = millis();

    if (burstActive && static_cast<long>(now - burstEndsAt) >= 0)
    {
      servo.writeMicroseconds(kStopPulseUs);
      burstActive = false;
      Serial.println("Burst complete; stopped at current position.");
    }

    if (alertActive && static_cast<long>(now - nextLedToggleAt) >= 0)
    {
      ledOn = !ledOn;
      digitalWrite(kLedPin, ledOn ? HIGH : LOW);
      nextLedToggleAt = now + kAlertLedToggleMs;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(kButtonPin, INPUT_PULLUP);
  pinMode(kLedPin, OUTPUT);
  pinMode(kBuzzerPin, OUTPUT);
  digitalWrite(kLedPin, LOW);
  digitalWrite(kBuzzerPin, LOW);

  servo.setPeriodHertz(50);
  servo.attach(kServoPin, 1000, 2000);
  if (!servo.attached())
  {
    Serial.println("ERROR: Could not attach servo. Check GPIO13.");
    while (true)
    {
      delay(1000);
    }
  }

  servo.writeMicroseconds(kStopPulseUs);
  Serial.println("Ready. Send alert to blink the LED and sound the buzzer; press GPIO27 to acknowledge.");
  Serial.println("After the press, button input is disabled for 3 seconds. Send alert again after cooldown.");
  Serial.println("Adjust kBurstDurationMs to tune the turn; exact 17-degree steps need position feedback.");
}

void loop()
{
  handleSerial();
  handleButton();
  updateBurst();
}
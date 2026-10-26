// LED + BUZZER TEST - no camera, no sensors
// LED    -> IO12 (through resistor)
// Buzzer -> IO14 (VCC on 3V3, GND on GND)
//
// Runs 4 steps forever. Watch the LED and listen to the buzzer:
//   A: LED only
//   B: buzzer pin LOW   (most 3-pin active-low buzzer modules)
//   C: buzzer pin HIGH  (active-high buzzer modules)
//   D: square-wave tone (passive buzzers)
// Whichever of B / C / D makes the buzzer sound tells you the right
// BUZZER_ACTIVE_LOW / BUZZER_PASSIVE settings for scarecrow.ino.

#define LED_PIN    12
#define BUZZER_PIN 14

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);   // start with buzzer quiet (active-low type)
  Serial.println("TEST STARTED");
}

void loop() {
  // A: LED only
  Serial.println("A: LED ON (3 sec) - buzzer quiet");
  digitalWrite(LED_PIN, HIGH);
  delay(3000);
  digitalWrite(LED_PIN, LOW);
  delay(1000);

  // B: buzzer pin LOW
  Serial.println("B: BUZZER pin LOW (3 sec)");
  digitalWrite(BUZZER_PIN, LOW);
  delay(3000);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000);

  // C: buzzer pin HIGH, quiet pin LOW (active-high type)
  Serial.println("C: BUZZER pin HIGH (3 sec)");
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(3000);
  digitalWrite(BUZZER_PIN, LOW);
  delay(1000);

  // D: passive buzzer tone (square wave)
  Serial.println("D: BUZZER tone (3 sec)");
  unsigned long start = millis();
  while (millis() - start < 3000) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(185);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(185);
  }
  digitalWrite(BUZZER_PIN, HIGH);

  Serial.println("--- repeating ---");
  delay(2000);
}

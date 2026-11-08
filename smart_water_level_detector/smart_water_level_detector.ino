/*
  Smart Water Level Detector and Controller
  Arduino Uno + HC-SR04 ultrasonic sensor + motor driver + DC motor (pump) + buzzer

  The ultrasonic sensor is mounted above the tank and measures the distance
  from the sensor to the water surface. As the tank fills, that distance
  gets smaller. The Arduino converts the distance to a water level, switches
  the pump motor through a motor driver, and sounds a buzzer when a limit is
  reached.

  Two operating modes (choose with OPERATING_MODE below):

  FILL_TANK  - the pump moves water INTO the tank.
               Tank full (surface closer than FULL_DISTANCE_CM):
                   motor OFF, buzzer ON  (the tank needs no more water)
               Water drops back by RESTART_MARGIN_CM:
                   motor ON, buzzer OFF

  DRAIN_TANK - the pump moves water OUT of the tank.
               Red zone (surface farther than LOW_DISTANCE_CM):
                   motor OFF, buzzer ON  (water shortage, refill the tank)
               Water refilled by RESTART_MARGIN_CM:
                   motor ON, buzzer OFF

  Each mode uses two thresholds (hysteresis), so the motor does not switch
  on and off rapidly when the water surface ripples around a single limit.

  Distance is calculated from the echo time:
      distance = (echo time x speed of sound) / 2
  The division by 2 is needed because the pulse travels to the water and back.
*/

// ---------- Operating mode ----------
enum OperatingMode { FILL_TANK, DRAIN_TANK };
const OperatingMode OPERATING_MODE = FILL_TANK;

// ---------- Pins ----------
const uint8_t TRIG_PIN   = 9;    // HC-SR04 trigger
const uint8_t ECHO_PIN   = 10;   // HC-SR04 echo
const uint8_t MOTOR_IN1  = 7;    // motor driver input 1
const uint8_t MOTOR_IN2  = 8;    // motor driver input 2
const uint8_t MOTOR_EN   = 5;    // motor driver enable (PWM pin)
const uint8_t BUZZER_PIN = 4;    // active buzzer

// ---------- Tank geometry and thresholds (in cm) ----------
// Distance from the sensor to the bottom of the empty tank.
const float EMPTY_DISTANCE_CM = 13.0;
// Water surface closer than this means the tank is full.
const float FULL_DISTANCE_CM  = 3.0;
// Water surface farther than this means the water level is in the red zone.
const float LOW_DISTANCE_CM   = 10.0;
// How far the level must move back before the motor restarts.
const float RESTART_MARGIN_CM = 3.0;

// ---------- Measurement settings ----------
const float SPEED_OF_SOUND_CM_PER_US = 0.0343;  // about 343 m/s at 20 degrees C
const unsigned long ECHO_TIMEOUT_US  = 12000;   // about 2 m, far more than needed
const uint8_t NUM_SAMPLES            = 5;       // readings per measurement
const uint8_t MIN_VALID_SAMPLES      = 3;       // readings needed for a result
const unsigned long SAMPLE_GAP_MS    = 60;      // pause between pings
const uint8_t MAX_BAD_MEASUREMENTS   = 5;       // stop the motor after this many failures

bool motorOn = false;
bool alarmOn = false;
bool firstReading = true;
uint8_t badMeasurements = 0;

// Send one ultrasonic pulse and return the distance in cm, or -1 if no echo.
float readDistanceOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long echoTime = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (echoTime == 0) {
    return -1.0;
  }
  return (echoTime * SPEED_OF_SOUND_CM_PER_US) / 2.0;
}

// Take several readings and return the median, which ignores stray values
// caused by ripples or echoes. Returns -1 if too few readings were valid.
float measureDistance() {
  float samples[NUM_SAMPLES];
  uint8_t count = 0;

  for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
    float d = readDistanceOnce();
    if (d > 0) {
      samples[count++] = d;
    }
    delay(SAMPLE_GAP_MS);
  }

  if (count < MIN_VALID_SAMPLES) {
    return -1.0;
  }

  // Simple insertion sort, then take the middle value.
  for (uint8_t i = 1; i < count; i++) {
    float key = samples[i];
    int8_t j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }
  return samples[count / 2];
}

void setMotor(bool on) {
  if (on) {
    digitalWrite(MOTOR_IN1, HIGH);
    digitalWrite(MOTOR_IN2, LOW);
    analogWrite(MOTOR_EN, 255);
  } else {
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    analogWrite(MOTOR_EN, 0);
  }
}

// Decide the motor and buzzer state from the measured distance.
void updateControl(float distance) {
  if (OPERATING_MODE == FILL_TANK) {
    if (firstReading) {
      alarmOn = (distance <= FULL_DISTANCE_CM);
      motorOn = !alarmOn;
    } else if (distance <= FULL_DISTANCE_CM) {
      motorOn = false;                 // tank full: stop pumping in
      alarmOn = true;
    } else if (distance >= FULL_DISTANCE_CM + RESTART_MARGIN_CM) {
      motorOn = true;                  // level has dropped: pump again
      alarmOn = false;
    }
  } else {
    if (firstReading) {
      alarmOn = (distance >= LOW_DISTANCE_CM);
      motorOn = !alarmOn;
    } else if (distance >= LOW_DISTANCE_CM) {
      motorOn = false;                 // red zone: stop pumping out
      alarmOn = true;
    } else if (distance <= LOW_DISTANCE_CM - RESTART_MARGIN_CM) {
      motorOn = true;                  // tank refilled: pump again
      alarmOn = false;
    }
  }
  firstReading = false;
}

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_EN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  setMotor(false);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(9600);
  Serial.println("Smart water level detector ready");
  Serial.println(OPERATING_MODE == FILL_TANK ? "Mode: fill tank" : "Mode: drain tank");
}

void loop() {
  float distance = measureDistance();

  if (distance < 0) {
    // No reliable reading. Stop the motor if this keeps happening.
    badMeasurements++;
    if (badMeasurements >= MAX_BAD_MEASUREMENTS) {
      motorOn = false;
      setMotor(false);
    }
    Serial.println("Sensor error: no valid echo");
    return;
  }
  badMeasurements = 0;

  updateControl(distance);
  setMotor(motorOn);
  digitalWrite(BUZZER_PIN, alarmOn ? HIGH : LOW);

  // Convert the distance to a water level percentage.
  float levelPercent = (EMPTY_DISTANCE_CM - distance) /
                       (EMPTY_DISTANCE_CM - FULL_DISTANCE_CM) * 100.0;
  if (levelPercent < 0) levelPercent = 0;
  if (levelPercent > 100) levelPercent = 100;

  Serial.print("Distance: ");
  Serial.print(distance, 1);
  Serial.print(" cm | Level: ");
  Serial.print(levelPercent, 0);
  Serial.print(" % | Motor: ");
  Serial.print(motorOn ? "ON" : "OFF");
  Serial.print(" | Buzzer: ");
  Serial.println(alarmOn ? "ON" : "OFF");
}

/////////////////////////////////////////////////////////////////
/*
  ESP8266/Arduino Library for reading rotary encoder values.
  Copyright 2017 Lennart Hennigs.
*/
/////////////////////////////////////////////////////////////////

#include "ESPRotary.h"

/////////////////////////////////////////////////////////////////



ESPRotary::ESPRotary(int pin1, int pin2, int moves_per_click ) {
  this->pin1 = pin1;
  this->pin2 = pin2;
  if (moves_per_click < 1) {
    moves_per_click = 1;
  }
  this->moves_per_click = moves_per_click;

  pinMode(pin1, INPUT_PULLUP);
  pinMode(pin2, INPUT_PULLUP);

  loop();
  resetPosition();
  last_read_ms = 0;
}

/////////////////////////////////////////////////////////////////

void ESPRotary::setChangedHandler(CallbackFunction f) {
  change_cb = f;
}

/////////////////////////////////////////////////////////////////

void ESPRotary::setRightRotationHandler(CallbackFunction f) {
  right_cb = f;
}

/////////////////////////////////////////////////////////////////

void ESPRotary::setLeftRotationHandler(CallbackFunction f) {
  left_cb = f;
}

/////////////////////////////////////////////////////////////////

void ESPRotary::resetPosition() {
  last_position = 0;
  position = 0;
  direction = 0;
}

/////////////////////////////////////////////////////////////////

byte ESPRotary::getDirection() {
  return direction;
}

/////////////////////////////////////////////////////////////////

String ESPRotary::directionToString(byte direction) {
  if (direction == RE_LEFT) {
    return "LEFT";
  } else {
    return "RIGHT";
    }
}

/////////////////////////////////////////////////////////////////

int ESPRotary::getPosition() {
  return position / moves_per_click;
}

/////////////////////////////////////////////////////////////////

void ESPRotary::loop() {
    int s = state & 3;
    if (digitalRead(pin1)) s |= 4;
    if (digitalRead(pin2)) s |= 8;

      switch (s) {
        case 0: case 5: case 10: case 15:
          break;
        case 1: case 7: case 8: case 14:
          position++; break;
        case 2: case 4: case 11: case 13:
          position--; break;
        case 3: case 12:
          position += 2; break;
        default:
          position -= 2; break;
      }
      state = (s >> 2);

      if (position != last_position) {
    if (position - last_position >= moves_per_click ||
      position - last_position <= -moves_per_click) {
      if (position > last_position) {
        direction = RE_RIGHT;
        if (right_cb != NULL) right_cb (*this);
      } else {
        direction = RE_LEFT;
        if (left_cb != NULL) left_cb (*this);
      }
      last_position = position;

      if (change_cb != NULL) change_cb (*this);
    }
      }
}

/////////////////////////////////////////////////////////////////

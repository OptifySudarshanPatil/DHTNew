//
//    FILE: dhtnew.cpp
//  AUTHOR: Rob.Tillaart@gmail.com
// VERSION: 0.3.2
// PURPOSE: DHT Temperature & Humidity Sensor library for Arduino
//     URL: https://github.com/RobTillaart/DHTNEW
//
// HISTORY:
// 0.1.0  2017-07-24 initial version based upon DHTStable
// 0.1.1  2017-07-29 add begin() to determine type once and for all instead of every call + refactor
// 0.1.2  2018-01-08 improved begin() + refactor()
// 0.1.3  2018-01-08 removed begin() + moved detection to read() function
// 0.1.4  2018-04-03 add get-/setDisableIRQ(bool b)
// 0.1.5  2019-01-20 fix negative temperature DHT22 - issue #120
// 0.1.6  2020-04-09 #pragma once, readme.md, own repo
// 0.1.7  2020-05-01 prevent premature read; add waitForReading flag (Kudo's to Mr-HaleYa),
// 0.2.0  2020-05-02 made temperature and humidity private (Kudo's to Mr-HaleYa),
// 0.2.1  2020-05-27 Fix #11 - Adjust bit timing threshold
// 0.2.2  2020-06-08 added ERROR_SENSOR_NOT_READY and differentiate timeout errors
// 0.3.0  2020-06-12 added getReadDelay & setReadDelay to tune reading interval
//                   removed get/setDisableIRQ; adjusted wakeup timing; refactor
// 0.3.1  2020-07-08 added powerUp() powerDown();
// 0.3.2  2020-07-17 fix #23 added get/setSuppressError(); overrulable DHTLIB_INVALID_VALUE

#include "dhtnew.h"

// these defines are not for user to adjust
#define DHTLIB_DHT11_WAKEUP        18
#define DHTLIB_DHT_WAKEUP          1

// READ_DELAY for blocking read
// datasheet: DHT11 = 1000 and DHT22 = 2000 
// use setReadDelay() to overrule (at own risk)
// as individual sensors can be read faster.
// see example DHTnew_setReadDelay.ino
#define DHTLIB_DHT11_READ_DELAY    1000
#define DHTLIB_DHT22_READ_DELAY    2000

// max timeout is 100 usec.
// loops using TIMEOUT use at least 4 clock cycli
// - read IO
// - compare IO
// - compare loopcounter
// - decrement loopcounter
//
// For a 16Mhz (UNO) 100 usec == 1600 clock cycles
// ==> 100 usec takes max 400 loops
// for a 240MHz (ESP32) 100 usec == 24000 clock cycles
// ==> 100 usec takes max 6000 loops
//
// By dividing F_CPU by 40000 we "fail" as fast as possible
#define DHTLIB_TIMEOUT (F_CPU/40000)


/////////////////////////////////////////////////////
//
// PUBLIC
//
DHTNEW::DHTNEW(uint8_t pin)
{
  _dataPin = pin;
  // Data-bus's free status is high voltage level.
  pinMode(_dataPin, OUTPUT);
  digitalWrite(_dataPin, HIGH);
  _readDelay = 0;
};

void DHTNEW::setType(uint8_t type)
{
  if ((type == 0) || (type == 11) || (type == 22))
  {
    _type = type;
  }
}

// return values:
// DHTLIB_OK
// DHTLIB_ERROR_CHECKSUM
// DHTLIB_ERROR_BIT_SHIFT
// DHTLIB_ERROR_SENSOR_NOT_READY
// DHTLIB_ERROR_TIMEOUT_A
// DHTLIB_ERROR_TIMEOUT_B
// DHTLIB_ERROR_TIMEOUT_C
// DHTLIB_ERROR_TIMEOUT_D
int DHTNEW::read()
{
  if (_readDelay == 0)
  { 
    _readDelay = DHTLIB_DHT22_READ_DELAY;
    if (_type == 11) _readDelay = DHTLIB_DHT11_READ_DELAY;
  }
  if (_type != 0)
  {
    while (millis() - _lastRead < _readDelay)
    {
      if (!_waitForRead) return DHTLIB_OK;
      yield();
    }
    return _read();
  }

  _type = 22;
  _wakeupDelay = DHTLIB_DHT_WAKEUP;
  int rv = _read();
  if (rv == DHTLIB_OK) return rv;

  _type = 11;
  _wakeupDelay = DHTLIB_DHT11_WAKEUP;
  rv = _read();
  if (rv == DHTLIB_OK) return rv;

  _type = 0; // retry next time
  return rv;
}

// return values:
// DHTLIB_OK
// DHTLIB_ERROR_CHECKSUM
// DHTLIB_ERROR_BIT_SHIFT
// DHTLIB_ERROR_SENSOR_NOT_READY
// DHTLIB_ERROR_TIMEOUT_A
// DHTLIB_ERROR_TIMEOUT_B
// DHTLIB_ERROR_TIMEOUT_C
// DHTLIB_ERROR_TIMEOUT_D
int DHTNEW::_read()
{
  // READ VALUES
  int rv = _readSensor();
  interrupts();

  // Data-bus's free status is high voltage level.
  pinMode(_dataPin, OUTPUT);
  digitalWrite(_dataPin, HIGH);
  _lastRead = millis();

  if (rv != DHTLIB_OK)
  {
    if (_suppressError == false)
    {
      _humidity    = DHTLIB_INVALID_VALUE;
      _temperature = DHTLIB_INVALID_VALUE;
    }
    return rv;  // propagate error value
  }

  if (_type == 22) // DHT22, DHT33, DHT44, compatible
  {
    _humidity =    (_bits[0] * 256 + _bits[1]) * 0.1;
    _temperature = ((_bits[2] & 0x7F) * 256 + _bits[3]) * 0.1;
  }
  else // if (_type == 11)  // DHT11, DH12, compatible
  {
    _humidity = _bits[0] + _bits[1] * 0.1;
    _temperature = _bits[2] + _bits[3] * 0.1;
  }

  if (_bits[2] & 0x80)  // negative temperature
  {
    _temperature = -_temperature;
  }
  _humidity = constrain(_humidity + _humOffset, 0, 100);
  _temperature += _tempOffset;

  // TEST CHECKSUM
  uint8_t sum = _bits[0] + _bits[1] + _bits[2] + _bits[3];
  if (_bits[4] != sum)
  {
    return DHTLIB_ERROR_CHECKSUM;
  }
  return DHTLIB_OK;
}

void DHTNEW::powerUp()
{
  digitalWrite(_dataPin, HIGH);
  // do a dummy read to sync the sensor
  read();
};

void DHTNEW::powerDown()
{
  digitalWrite(_dataPin, LOW);
}


/////////////////////////////////////////////////////
//
// PRIVATE
//

// return values:
// DHTLIB_OK
// DHTLIB_ERROR_CHECKSUM
// DHTLIB_ERROR_BIT_SHIFT
// DHTLIB_ERROR_SENSOR_NOT_READY
// DHTLIB_ERROR_TIMEOUT_A
// DHTLIB_ERROR_TIMEOUT_B
// DHTLIB_ERROR_TIMEOUT_C
// DHTLIB_ERROR_TIMEOUT_D
int DHTNEW::_readSensor()
{
  // INIT BUFFERVAR TO RECEIVE DATA
  uint8_t mask = 0x80;
  uint8_t idx = 0;

  // EMPTY BUFFER
  for (uint8_t i = 0; i < 5; i++) _bits[i] = 0;

  // HANDLE PENDING IRQ
  yield();  

  // REQUEST SAMPLE - SEND WAKEUP TO SENSOR
  pinMode(_dataPin, OUTPUT);
  digitalWrite(_dataPin, LOW);
  // add 10% extra for timing inaccuracies in sensor.
  delayMicroseconds(_wakeupDelay * 1100UL);

  // HOST GIVES CONTROL TO SENSOR
  pinMode(_dataPin, INPUT_PULLUP);

  // DISABLE INTERRUPTS when clock in the bits
  noInterrupts();

  // SENSOR PULLS LOW after 20-40 us  => if stays HIGH ==> device not ready
  uint16_t loopCnt = DHTLIB_TIMEOUT;
  while(digitalRead(_dataPin) == HIGH)
  {
    if (--loopCnt == 0) return DHTLIB_ERROR_SENSOR_NOT_READY;
  }

  // SENSOR STAYS LOW for ~80 us => or TIMEOUT
  loopCnt = DHTLIB_TIMEOUT;
  while(digitalRead(_dataPin) == LOW)
  {
    if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT_A;
  }

  // SENSOR STAYS HIGH for ~80 us => or TIMEOUT
  loopCnt = DHTLIB_TIMEOUT;
  while(digitalRead(_dataPin) == HIGH)
  {
    if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT_B;
  }

  // SENSOR HAS NOW SEND ACKNOWLEDGE ON WAKEUP
  // NOW IT SENDS THE BITS

  // READ THE OUTPUT - 40 BITS => 5 BYTES
  for (uint8_t i = 40; i != 0; i--)
  {
    // EACH BIT START WITH ~50 us LOW
    loopCnt = DHTLIB_TIMEOUT;
    while(digitalRead(_dataPin) == LOW)
    {
      if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT_C;
    }

    // DURATION OF HIGH DETERMINES 0 or 1
    // 26-28 us ==> 0
    //    70 us ==> 1
    uint32_t t = micros();
    loopCnt = DHTLIB_TIMEOUT;
    while(digitalRead(_dataPin) == HIGH)
    {
      if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT_D;
    }
    if ((micros() - t) > DHTLIB_BIT_THRESHOLD)
    {
      _bits[idx] |= mask;
    }

    // PREPARE FOR NEXT BIT
    mask >>= 1;
    if (mask == 0)   // next byte?
    {
      mask = 0x80;
      idx++;
    }
  }
  // After 40 bits the sensor pulls the line LOW for 50 us
  // TODO: should we wait?
  loopCnt = DHTLIB_TIMEOUT;
  while(digitalRead(_dataPin) == LOW)
  {
    if (--loopCnt == 0) break; // return DHTLIB_ERROR_TIMEOUT_E;
  }

  // CATCH RIGHTSHIFT BUG ESP (only 1 single bit shift)
  // humidity is max 1000 = 0x0E8 for DHT22 and 0x6400 for DHT11
  // so most significant bit may never be set.
  if (_bits[0] & 0x80) return DHTLIB_ERROR_BIT_SHIFT;

  return DHTLIB_OK;
}

// -- END OF FILE --

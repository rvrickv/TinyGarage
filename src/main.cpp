/*
 * Created on Wed Sep 30 2020
 *  
 * ATTiny85 Garage Parking assist
 * 
 * Copyright (c) 2020 Rick Villela
 * 
 * 
 * ATTiny 85 running at 8MHz internal clock
 * HC-SR04 Sonar
 * 7 LED WS2812 ring as a display. 
 * P-Channel MOSFET
 * 
 */


#include <Arduino.h>
#include <elapsedMillis.h>
#include <EEPROM.h>
#include <tinysnore.h>
//#include <NeoPixelBus.h>
#include <NeoPixelBrightnessBus.h>

#include <NewPing.h>

#include <Button.h>
#include <ButtonEventCallback.h>
#include <BasicButton.h>


// LED RING  DEFINITIONS
#define PIN_LED_RING  2
#define NUM_LEDS 7
#define LED_BRIGHTNESS 40
//#define LED_TYPE NEOPIXEL
#define MAX_AMP_DRAW  35

#define PIN_BUTTON 0 
#define LONG_PRESS 1000

// HC-SRO4 DEFINITIONS
#define PIN_TRIGGER 3
#define PIN_ECHO 4
#define MAX_DISTANCE 350
#define SAMPLES 5

#define PIN_MOSFET 1

#define PARKING_DISTANCE 100
#define TIME_TO_LED_OFF  5000  // time to LEDs off
#define TIME_TO_SLEEP 8000  // time to processor sleep
#define wakeUpInterval 5000  // wake up  timer y7 for ATTiny deep sleep


NewPing g_sonar(PIN_TRIGGER, PIN_ECHO, MAX_DISTANCE);

// initialize the LED ring
NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> g_leds(NUM_LEDS, PIN_LED_RING);

bool g_highPrecision = true;
uint16_t g_oldDistance = 0;
uint16_t g_ParkingDistance = PARKING_DISTANCE;


// Related to sleeping 
uint16_t g_parkedTime = 0;
elapsedMillis g_timeElapsed = 0;

// Button handling
BasicButton g_multiButton(PIN_BUTTON);

uint16_t roundToBase(uint16_t value){
  const uint16_t base = 5;

  if (g_highPrecision) {
    // round to the base based passed to the function
    return uint16_t(base * round(float(value)/base));
  }
  else
  {
    // use low precision logarithmic smooting. Values will be limited to
    // 10 20 30... 90 100 200 300
    uint8_t len = log10(value);
    float div = pow(10,len);
    return (ceil(value/div)*div);
  }
}

uint16_t reset_parking_distance(){

  uint16_t new_distance = roundToBase(g_sonar.convert_cm(g_sonar.ping_median(SAMPLES))); 
  new_distance += 3;  // a small buffer to ensure we're "Red"
  g_parkedTime = 0; // reset the parked time to "wakeup" lights and pinging

  EEPROM.put(0,new_distance);  // write the new distance to eeprom

  return (new_distance);
}

//  neopixelbus color definitions
RgbColor red(LED_BRIGHTNESS, 0, 0);
RgbColor green(0, LED_BRIGHTNESS, 0);
RgbColor blue(0, 0, LED_BRIGHTNESS);
RgbColor yellow(LED_BRIGHTNESS, LED_BRIGHTNESS,0);
RgbColor lightblue(0, LED_BRIGHTNESS, LED_BRIGHTNESS);
RgbColor black(0);

void showColor(RgbColor color){
   for(int i=0; i<NUM_LEDS; i++) { // For each pixel...
    g_leds.SetPixelColor(i,color);
    g_leds.Show();
  }
}

void onShortPress()
{
 // Serial.println("Button Press");
  g_highPrecision = !g_highPrecision;

 // save to eeprom at the second block after the parking distance which is a uint16_t 
  uint8_t ee_address = sizeof(uint16_t);
  EEPROM.put(ee_address, g_highPrecision);

  // show an up green arrow for high precision or a red down arrow for low precision
  uint8_t UpArrow[]= {0,1,4,2,6};
  uint8_t DownArrow[] = {0,1,4,5,3};

  // Show up or down arrow to indicate high or low precision. But first blank out the LEDs
  showColor(black);

  if (g_highPrecision){
    for (uint8_t i = 0; i<(sizeof(UpArrow)/sizeof(uint8_t)); i++)
      g_leds.SetPixelColor(UpArrow[i],green);
    
    g_leds.Show();
    delay (1000);
  } else {
    for (uint8_t i = 0; i<(sizeof(DownArrow)/sizeof(uint8_t)); i++) 
      g_leds.SetPixelColor(DownArrow[i],red);
    
    g_leds.Show();
    delay (1000);
  }

}

void onLongPress(Button& btn, uint16_t duration)
{
  // Do a brief countdown
  showColor(blue);
  for (int i = NUM_LEDS - 1; i >=0 ; i--){
    g_leds.SetPixelColor(i, black);
    g_leds.Show();
    delay(350);
  }

  g_ParkingDistance = reset_parking_distance();

}

void onButtonPressed(Button& btn, uint16_t duration){
  // ugly little hack -- change the old distance so the LEDs light up again
  g_oldDistance = 0;

  if (duration > LONG_PRESS )
    {}// do nothing, the onHold will take care of it
  else 
    onShortPress();
}

void setup() {
  // Initialize the LED strip 
  g_leds.Begin();
  g_leds.Show();
  
  pinMode(PIN_BUTTON, INPUT);
  pinMode(PIN_MOSFET, OUTPUT);

  // Initialize the button
  g_multiButton.onRelease(onButtonPressed);
  g_multiButton.onHold(LONG_PRESS,onLongPress);

  //Retrieve the parking distance from eeprom. If none, set it to default
  uint16_t ee_distance = 0;
  uint8_t ee_address = 0;
  EEPROM.get(ee_address,ee_distance);
  if (ee_distance)
    g_ParkingDistance = ee_distance;

  // Read from eeprom at the second block after the parking distance which is a uint16_t 
  bool precision = false;
  ee_address = sizeof(uint16_t);
  EEPROM.get(ee_address, precision);


  //Turn the HC-SR04 on
  digitalWrite(PIN_MOSFET,LOW);   //Not sure about this, but I think because I'm using a P-channel, I need ot pull the pin low


    //testing
        showColor(yellow);
        delay(2000);
        showColor(black);
        delay(2000);

}



void loop() {


  uint16_t distance = g_sonar.convert_cm(g_sonar.ping_median(SAMPLES*2));  //Twice as many samples for better accuracy

  distance = roundToBase(distance);

  if (distance != g_oldDistance){
    g_oldDistance = distance;
    g_timeElapsed = 0;

    if (distance > g_ParkingDistance)
        showColor(green);
    if (distance <= g_ParkingDistance && distance > 0)
        showColor(red);
    if (distance == 0)
        showColor(black);
  } else {
    if (g_timeElapsed >= TIME_TO_LED_OFF) {
      // Fade to black
      // get original brightness firs so we can restore it
      uint8_t brightness = g_leds.GetBrightness();
      for (uint8_t i = brightness; i>10; i-=2){
        g_leds.SetBrightness(i);
        g_leds.Show();
        delay(10);
      }

      //restore previous brightness setting
      g_leds.SetBrightness(brightness);
      showColor(black);

      //put the ATTiny to sleep for the predefined interval
      if (g_timeElapsed >= TIME_TO_SLEEP)
      {
        digitalWrite(PIN_MOSFET,HIGH);  //Turn the HC-SR0
        snore(wakeUpInterval);  //Put the ATTiny to sleep for the appropriate interval

        // After snore, it should resume right here, so the next step is first to turn the HC-SR04 back on. We do that by setting the p-channel mosfet low (negative)
        digitalWrite(PIN_MOSFET,LOW);

        // pause 2000 ms for HCSR-04 and LEDs to stabilize
        delay(5000);

        showColor(lightblue);
        delay(2000);
        showColor(black);
        delay(2000);
        // take a "fake" reding to get the HCSR04 "primed"
        g_sonar.ping_median(SAMPLES);
        delay (5000);
      }
        
    }
  }
  
  g_multiButton.update();

}
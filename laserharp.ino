#include <Logging.h>

#ifdef GINSING
#include <GinSing.h>
#include <GinSingDefs.h>
#include <GinSingMaster.h>
#include <GinSingPoly.h>
#include <GinSingPreset.h>
#include <GinSingSerial.h>
#include <GinSingSerialMacros.h>
#include <GinSingSynth.h>
#include <GinSingVoice.h>

#include <Wire.h>
#include <GinSing.h>
#endif

#include <Adafruit_MotorShield.h>
#include "utility/Adafruit_PWMServoDriver.h"

#include "HarpNoteChoice.h"
#include "HarpNoteDetection.h"

//When debugging I wanted more information. So ... set this boolean to true
//to get more stuff printed to the console. When it's true the console dumps LOTS of
//great stuff - but the code CRAWLS and the laser harp isn't great.
const boolean debug = false;

boolean gPauseMotor = false;

// Create the motor shield object with the default I2C address
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
// Or, create it with a different I2C address (say for stacking)
// Adafruit_MotorShield AFMS = Adafruit_MotorShield(0x61);

// Connect a stepper motor with 200 steps per revolution (1.8 degree)
// to motor port #2 (M3 and M4)
Adafruit_StepperMotor *myMotor = AFMS.getStepper(200, 2);

#ifdef GINSING
GinSing  GS;
#define rcvPin  4  // this is the pin used for receiving    ( can be either 4 or 12 )
#define sndPin  3  // this is the pin used for transmitting ( can be either 3 or 11 ) 
#define ovfPin  2  // this is the pin used for overflow control ( can be either 2 or 10 )
GinSingPoly * poly;

#define GINSING0 0
#define GINSING1 1
#endif

const int lightSensorPin = 0;
const int sonarPin = 1;

const int stepSize = 1;
const int delayBetweenSteps = 15;
const int numberNotes = 7;

#ifdef GINSING
GSNote ginsingNotes[numberNotes + 1];
#endif

#ifdef ADJUSTER_BUTTONS
const int buttonApin = 9;
const int buttonBpin = 8;
#endif

HarpNoteChoice harpNoteChoice;
HarpNoteDetection harpNoteDetector;

boolean pluckedNotes[numberNotes];
int reflectedLightValues[numberNotes];


int gLogLevel = LOG_LEVEL_NOOUTPUT;
//int gLogLevel = LOG_LEVEL_ERRORS;
//int gLogLevel = LOG_LEVEL_INFOS;
//int gLogLevel = LOG_LEVEL_DEBUG;
//int gLogLevel = LOG_LEVEL_VERBOSE;

int gAverageLightReading = 0;
const unsigned long gBaud = 250000;

// buf must be an array with length > segmentCount. This function can write to buf[segmentCount]
void drawMeter(int value, int maxValue, char * buf, char segmentSymbol = '=', size_t totalSegments = 80)
{
    float scaleFactor = (float)totalSegments / maxValue;
    int segmentCount = (int)(0.5 + (min(value, maxValue) * scaleFactor));
    memset(buf, segmentSymbol, segmentCount);
    buf[segmentCount] = 0;
}

void logMeter(Logging & Log, int value, int maxValue, char segmentSymbol = '=')
{
  char buf[81] = {0};
  drawMeter(value, maxValue, buf, segmentSymbol, sizeof(buf) - 1);
  Log.Info("%s\n", buf);
}

int sampleLight(int sampleCount, int interval=2)
{
  unsigned long average = analogRead(lightSensorPin);
  for (int i = 1; i < sampleCount; i++) 
  {
    delay(interval);
    int sample = analogRead(lightSensorPin);
    average *= i;
    average += sample;
    average /= i + 1; 
    Log.Info("sample %d: %d, average - %d\n", i, sample, average);
  }
  return (int)average;
}

void setup()
{
  Log.Init(gLogLevel, gBaud);
  harpNoteDetector.setNumNotes(numberNotes);

  //For the light sensor
  analogReference(EXTERNAL);

#ifdef ADJUSTER_BUTTONS
  pinMode(buttonApin, INPUT_PULLUP);
  pinMode(buttonBpin, INPUT_PULLUP);
#endif

  AFMS.begin();  // create with the default frequency 1.6KHz
  myMotor->setSpeed(250);

  //Get some initial values for each light string
  gAverageLightReading = sampleLight(50);
  for (int i = 0; i < numberNotes; i++)
  {
    reflectedLightValues[i] = analogRead(lightSensorPin);
    pluckedNotes[i] = false;
  }

#ifdef GINSING
  setupGinSing();
#endif
}

#ifdef GINSING
//This is the code required to get GinSing ready to go.
void setupGinSing()
{
  GS.begin(rcvPin, sndPin, ovfPin);               // start the device (required) and enter preset mode
  //For preset mode - change this when going to poly mode
  poly = GS.getPoly();
  poly->begin();                                    // enter presetmode

  poly->setWaveform(GINSING0, SAWTOOTH);
  poly->setWaveform(GINSING1, SAWTOOTH);

  setupGinsingNotes();
}

//These are the notes I want to play with GinSing.
void setupGinsingNotes()
{
  ginsingNotes[0] = C_4;
  ginsingNotes[1] = D_4;
  ginsingNotes[2] = E_4;
  ginsingNotes[3] = F_4;
  ginsingNotes[4] = G_4;
  ginsingNotes[5] = A_4;
  ginsingNotes[6] = B_4;
}
#endif

//This code is used to take the sonar reading and convert
//that into something to change the GinSing note being played.
int findMultiplier(int height)
{
  /*
    Over 170 = nothing
    140 = high           80 - 5
    125 = normal  64 - 4
    110 = low            48 - 3
    92 = very low       32 - 2
    74 = lower          16 - 1
    62 bottom out
  */
  if (height > 135) return 5;
  if (height > 119) return 4;
  if (height > 100) return 3;
  if (height > 80) return 2;
  //My speaker gets weird at the low values. So I commented that one out.
  //	if (height > 65) return 1;
  return 0;
}

//Read the sonar unit and figure out if the
//notes should move up or down
void checkSonar()
{
  int height = analogRead(sonarPin);

  logMeter(Log, height, 160);

  if (height > 170)
  {
    return;
  }
  int multiplier = findMultiplier(height);
  int baseValue = 16 * multiplier;
#ifdef GINSING
  ginsingNotes[0] = (GSNote)baseValue;
  ginsingNotes[1] = (GSNote)(baseValue + 1);
  ginsingNotes[2] = (GSNote)(baseValue + 2);
  ginsingNotes[3] = (GSNote)(baseValue + 3);
  ginsingNotes[4] = (GSNote)(baseValue + 4);
  ginsingNotes[5] = (GSNote)(baseValue + 5);
  ginsingNotes[6] = (GSNote)(baseValue + 6);
#endif
}

#ifdef GINSING
//These two are the notes that Ginsing is currently playing.
int ginsingNote0 = -1;
int ginsingNote1 = -1;
#endif

//FirstNote and SecondNote are the notes we want to be playing.
void playNote(int firstNote, int secondNote)
{

  Log.Verbose("Playing notes %d and %d\n", firstNote, secondNote);

  //Pick the notes to be played and which channel to play them on
#if GINSING
  harpNoteChoice.pickNotes(ginsingNote0, ginsingNote1, firstNote, secondNote);
  if (ginsingNote0 == -1)
  {
    poly->release(GINSING0);
  }
  else
  {
    poly->setNote(GINSING0, ginsingNotes[ginsingNote0]);
    poly->trigger(GINSING0);
  }

  if (ginsingNote1 == -1)
  {
    poly->release(GINSING1);
  }
  else
  {
    poly->setNote(GINSING1, ginsingNotes[ginsingNote1]);
    poly->trigger(GINSING1);
  }
#endif
}

//Move the motor one step. Sleep, then take a light reading. The sleep
//gives the sensor time to actually report the new reading.
int stepTheMotorAndGetLightReading(int directionToStep)
{
  if (gPauseMotor == false)
    myMotor->step(stepSize, directionToStep, DOUBLE);
  unsigned long duration = millis() + delayBetweenSteps;
  while (millis() < duration)
  {
    int light = analogRead(lightSensorPin);
    logMeter(Log, light, 600, '.');

    if (light > (gAverageLightReading * 1.1)) 
    {
      Log.Info("Playing Note!\n");
    }
  }
  int currentLightReading = analogRead(lightSensorPin);

  return currentLightReading;
}

void checkNotes(int reflectedLightValues[], boolean pluckedNotes[])
{
  for (int i = 0; i < numberNotes; i++)
  {
    Log.Verbose("%d=%d - ", i, reflectedLightValues[i]);
  }
  Log.Verbose("\n");

  //So ... are any light readings different from any others?
  //To find the anomaly, see how different each string is to every other string.
  //One or two strings should stand out. Those are the plucked strings.
  harpNoteDetector.checkNotes(reflectedLightValues, pluckedNotes);

  //So what strings are plucked? Note that if more than two are plucked,
  //that counts as an error ... if that's the case, just keep playing the current notes
  //and hope the player gets his act straight.
  int firstNote = -1;
  int secondNote = -1;
  if (harpNoteDetector.getNotes(firstNote, secondNote, pluckedNotes))
  {
    if (firstNote >= 0)
    {
      Log.Verbose("==============PLUCKED 1: %d\n", firstNote);
    }
    if (secondNote >= 0)
    {
      Log.Verbose("==============PLUCKED 2: %d\n", secondNote);
    }
    playNote(firstNote, secondNote);
  }
  else
  {
    Log.Verbose("getNotes returned false - more than three notes were counted as plucked.\n");
  }
  return;
}

//Is a button pressed? If so move the motor a bit. This lets the user adjust the laser fan so it's pointing upwards properly.
void checkButtons()
{
#ifdef ADJUSTER_BUTTONS
  if (digitalRead(buttonApin) == LOW)
  {
    myMotor->step(stepSize, BACKWARD, DOUBLE);
  }

  if (digitalRead(buttonBpin) == LOW)
  {
    myMotor->step(stepSize, FORWARD, DOUBLE);
  }
#else
  while (Serial.available() > 0)
  {
    char c = Serial.read();
    switch (c)
    {
      case 'f':
        myMotor->step(stepSize, FORWARD, DOUBLE);
        break;
      case 'b':
        myMotor->step(stepSize, BACKWARD, DOUBLE);
        break;
      case 'p':
        gPauseMotor = !gPauseMotor;
        break;
      case 'l':
        gLogLevel = (gLogLevel + 1) % (LOG_LEVEL_VERBOSE + 1);
        //Log.Init(9600, gLogLevel);
        static const  char* levelStrings[]  = {"DISABLED","VERBOSE", "DEBUG", "INFO", "ERROR"};
        Serial.print("Log level is "); Serial.println(levelStrings[gLogLevel]);
      default:
        Log.Error("Ignorning unknown command: %c\n", c);
        
    }
  }
#endif
}

void loop()
{

  //There must be at least a handful notes for the code below to work right.
  if (numberNotes < 5)
  {
    return;
  }

  //Run the laser forward, read all values, and see what is there. Note that this pretty much uses one
  //less note than requested - but the START position counts as a spot. So moving it numberNotes makes that many
  //strings plus the start string.

  //It's already read the zero item. So read array items 1 through 7.
  for (int i = 1; i < numberNotes; i++)
  {
    reflectedLightValues[i] = stepTheMotorAndGetLightReading(FORWARD);
    checkNotes(reflectedLightValues, pluckedNotes);
  }

  checkSonar();

  //It just read item 7. So going backwards, read items 6 through zero.
  for (int i = numberNotes - 2; i >= 0; i--)
  {
    reflectedLightValues[i] = stepTheMotorAndGetLightReading(BACKWARD);
    checkNotes(reflectedLightValues, pluckedNotes);
  }

  checkButtons();
  checkSonar();

}


#include <SerialCommand.h>

#include <Logging.h>

#include <Adafruit_MotorShield.h>
#include "utility/Adafruit_PWMServoDriver.h"

#include <GinSing.h>   

#define BAUD             115200
#define LIGHT_SENSOR_PIN      0
#define RANGE_SENSOR_PIN      1

#define MIDICMD_NOTEON     0x90 // MIDI command (Note On, Channel 0)
#define MIDICMD_NOTEOFF    0x80 // MIDI command (Note On, Channel 0)

#define NOTE_COUNT_MAX      10 // kinda a hack to allow static initialization in waitForNote

GinSing  gGS;            // this creates the class library from which all methods and data are accessed
GinSingPoly * gGSPoly =     NULL;

GSNote gGSNote[NOTE_COUNT_MAX] = {C_6, CS_6, D_6, DS_6, E_6, F_6, FS_6, G_6, GS_6, A_6};

bool gMidi             =   false;
bool gGinsing          =   true;
int  gNoteCount        =   10;
bool gMute             =   false;

int gMotorStepSize    =   4;
int gMotorStepsPerRev = 200;
int gMotorStepDelay   =  10; // set to <= 0 to pause motor

int    gLightAverage     = 0;
double gLightSensitivity = 5;

//int gLogLevel = LOG_LEVEL_NOOUTPUT;
int gLogLevel = LOG_LEVEL_ERRORS;
//int gLogLevel = LOG_LEVEL_INFOS;
//int gLogLevel = LOG_LEVEL_DEBUG;
//int gLogLevel = LOG_LEVEL_VERBOSE;

Adafruit_MotorShield AFMS = Adafruit_MotorShield();
Adafruit_StepperMotor *myMotor = AFMS.getStepper(gMotorStepsPerRev, 2);

SerialCommand sCmd;


// buf must be an array with length > segmentCount. This function can write to buf[segmentCount]
void drawMeter(int value, int maxValue, char * buf, char segmentSymbol = '=', size_t totalSegments = 80)
{
    float scaleFactor = (float)totalSegments / maxValue;
    int segmentCount = (int)(0.5 + (min(value, maxValue) * scaleFactor));
    memset(buf, segmentSymbol, segmentCount);
    buf[segmentCount] = 0;
}

void logMeter(Logging & Log, int value, int maxValue, int level = LOG_LEVEL_INFOS, char segmentSymbol = '=')
{
  char buf[81] = {0};
  drawMeter(value, maxValue, buf, segmentSymbol, sizeof(buf) - 1);
  switch (level)
  {
    case LOG_LEVEL_ERRORS:
      Log.Error("%s\n", buf);
      break;
    case LOG_LEVEL_INFOS:
      Log.Info("%s\n", buf);
      break;
    case LOG_LEVEL_DEBUG:
      Log.Debug("%s\n", buf);
      break;
    case LOG_LEVEL_VERBOSE:
      Log.Verbose("%s\n", buf);
      break;
    case LOG_LEVEL_NOOUTPUT:
    default:
      break;
  }
}

int runningAverage(int average, int sample, int index) {

  unsigned long sum = average * index;
  sum += sample;
  return (int)(sum /= (index + 1));
  
}

int sampleLight(int sampleCount, int interval=2)
{
  int average = 0;
  for (int i = 0; i < sampleCount; i++) 
  {
    delay(interval);
    int sample = analogRead(LIGHT_SENSOR_PIN);
    average = runningAverage(average, sample, i);
    Log.Info("sample %d: %d, average - %d\n", i, sample, average);
  }
  return max(1, (int)average);
}

void stepMotor(int directionToStep=FORWARD, int steps = -1)
{
  if (steps == -1) steps = gMotorStepSize;
  
  myMotor->step(steps, directionToStep, DOUBLE);
}

void autoPositionMirror() {
  int maxLight = 0;
  int maxLightStep = 0;
  for (int i = 0; i < gMotorStepsPerRev; i++) {
    if (i % (gMotorStepsPerRev / 10) == 0)
    {
      Serial.print(F("."));
    }
    stepMotor(FORWARD, 1);
    int sample = sampleLight(30);
    logMeter(Log, sample, 600, LOG_LEVEL_INFOS);
    if (sample > maxLight) 
    {
      maxLight = sample;
      maxLightStep = i;
    }
  }
  Log.Info("Max light value (%d) at motor step %d\n", maxLight, maxLightStep);
  stepMotor(FORWARD, maxLightStep + (gMotorStepsPerRev / 8));
}

void setupMotor() {
  Serial.print(F("Setting up motor..."));
  AFMS.begin();  // create with the default frequency 1.6KHz
  myMotor->setSpeed(250);
  autoPositionMirror();
  Serial.println(F("done"));
  
}

void sampleLightCmd()
{
  Serial.print(F("Running ")); Serial.println(__FUNCTION__);

  Serial.print(F("Before: ")); Serial.println(gLightAverage);
  gLightAverage = sampleLight(50);
  Serial.print(F("After:  ")); Serial.println(gLightAverage);
}

void setLightSensitivityCmd() 
{
  Serial.print(F("Running ")); Serial.println(__FUNCTION__);

  char * str = sCmd.next();
  if (str && *str)
  {
    double val = atof(str);
    if (val > 1)
    {
      Serial.print(F("Before: ")); Serial.println(gLightSensitivity);
      gLightSensitivity = val;
      Serial.print(F("After:  ")); Serial.println(gLightSensitivity);
    }
    else 
    {
      Serial.print(F("Invalid value: "));
      Serial.print(val);
      Serial.println(F(". Must be > 1."));
    }
  }
  else
  {
    Serial.print(F("Invalid input: \""));
    Serial.print(str);
    Serial.println(F("\""));
  }
}

void setNoteCountCmd() 
{
  Serial.print(F("Running ")); Serial.println(__FUNCTION__);

  char * str = sCmd.next();
  if (str && *str)
  {
    int val = atoi(str);
    if (val > 1 && val <= NOTE_COUNT_MAX)
    {
      Serial.print(F("Before: ")); Serial.println(gNoteCount);
      gNoteCount = val;
      Serial.print(F("After:  ")); Serial.println(gNoteCount);
    }
    else 
    {
      Serial.print(F("Invalid value: "));
      Serial.print(val);
      Serial.println(F(". Must be > 1."));
    }
  }
  else
  {
    Serial.print(F("Invalid input: \""));
    Serial.print(str);
    Serial.println(F("\""));
  }
}
void unrecognizedSerialCmd(const char * cmd) 
{
  Serial.print(F("Unrecognized serial command: "));
  Serial.println(cmd);  
}

void playMidiNote(bool on, int note) 
{
  if (gMidi == false)
  {
    return;
  }
  char cmd = on ? MIDICMD_NOTEON : MIDICMD_NOTEOFF;
  char data1 = 0x4a + note;
  char data2 = 0x49;
  Serial.print(cmd);
  Serial.print(data1);
  Serial.print(data2);
}

void playGinsingNote(bool on, int note)
{
  if (gGinsing == false)
  {
    return;
  }

  if (on == true)
  {
    gGSPoly->setNote(1, gGSNote[note]);
    gGSPoly->trigger(1);
  }
  else
  {
    gGSPoly->release(1);
  }
}
  
  
void playNote(bool on, int note)
{
  if (on == true && gMute == true) 
  {
    return;
  }

  playMidiNote(on, note);

  playGinsingNote(on, note);
}


void playNotesCmd()
{
  Serial.println(F("Running playNoteCmd"));
  Serial.print(F("Playing notes: "));
  for (int iNote = 0; iNote < NOTE_COUNT_MAX; iNote++)
  {
    Serial.print(iNote); Serial.print(F(" "));
    playNote(true, iNote);
    delay(250);
    playNote(false, iNote);
  }
  Serial.println();
}

void setupSerialCommands()
{
  sCmd.addCommand("SampleLight", sampleLightCmd);
  sCmd.addCommand("SetLightSensitivity", setLightSensitivityCmd);
  sCmd.addCommand("SetNoteCount", setNoteCountCmd);
  sCmd.addCommand("AutoPosition", autoPositionMirror);
  sCmd.addCommand("PlayNotes", playNotesCmd);
  sCmd.setDefaultHandler(unrecognizedSerialCmd);  
}

void setupGinsing()
{
  if (gGinsing == false)
  {
    return;
  }
  
  Serial.println(F("Setting up ginsing"));

#define rcvPin  4       // this is the pin used for receiving    ( can be either 4 or 12 )
#define sndPin  3       // this is the pin used for transmitting ( can be either 3 or 11 ) 
#define ovfPin  2       // this is the pin used for overflow control ( can be either 2 or 10 )
  gGS.begin( rcvPin , sndPin , ovfPin );

//  GinSingVoice *voice = gGS.getVoice();                    // get the interface for voice mode
//  voice->begin();                                         // enter voice mode
//  voice->preview();                                       // preview the mode

  gGSPoly = gGS.getPoly();                           // get the poly mode interface
  gGSPoly->begin ();  // enter poly mode
}

void setup()
{
  setupSerialCommands();

  Log.Init(gLogLevel, BAUD);

  //For the light sensor
  analogReference(EXTERNAL);

  setupGinsing();
  
  setupMotor();  

  //Get some initial values for each light string
  gLightAverage = sampleLight(50);

}

//Read the sonar unit and figure out if the
//notes should move up or down
void checkSonar()
{
  int height = analogRead(RANGE_SENSOR_PIN);

  logMeter(Log, height, 160);

}

void waitForNote(int note, int delay) 
{
  static bool noteStates[NOTE_COUNT_MAX] = {0};
  bool detected = false;
  bool currentlyOn = noteStates[note];
  unsigned long duration = millis() + delay;
  while (millis() < duration)
  {
    int light = analogRead(LIGHT_SENSOR_PIN);
    logMeter(Log, light, 600, LOG_LEVEL_INFOS, '.');
    
    detected = (light > (gLightAverage * gLightSensitivity));

    if (currentlyOn != detected)
    {
      Log.Info("Note %d %s!\n", note, detected ? "on" : "off");
      playNote(detected, note);
    }
          
  }
  noteStates[note] = detected;
}

void printInfo()
{
  Serial.print(F("BAUD                ")); Serial.println(BAUD);
  Serial.print(F("LIGHT_SENSOR_PIN    ")); Serial.println(LIGHT_SENSOR_PIN);
  Serial.print(F("RANGE_SENSOR_PIN    ")); Serial.println(RANGE_SENSOR_PIN);

  Serial.print(F("MIDICMD_NOTEON      ")); Serial.println(MIDICMD_NOTEON);
  Serial.print(F("MIDICMD_NOTEOFF     ")); Serial.println(MIDICMD_NOTEOFF);

  Serial.print(F("NOTE_COUNT_MAX      ")); Serial.println(NOTE_COUNT_MAX);

  Serial.print(F("gNoteCount        = ")); Serial.println(gNoteCount);
  Serial.print(F("gMute             = ")); Serial.println(gMute ? F("true") : F("false"));
  
  Serial.print(F("gMotorStepSize    = ")); Serial.println(gMotorStepSize);
  Serial.print(F("gMotorStepsPerRev = ")); Serial.println(gMotorStepsPerRev);
  Serial.print(F("gMotorStepDelay   = ")); Serial.println(gMotorStepDelay);

  Serial.print(F("gLightAverage     = ")); Serial.println(gLightAverage);
  Serial.print(F("gLightSensitivity = ")); Serial.println(gLightSensitivity);

  Serial.print(F("gLogLevel         = ")); Serial.println(gLogLevel);
}

void checkButtons()
{
  while (Serial.available() > 0)
  {
    char c = Serial.read();
    Serial.print(F("Read character from serial: ")); Serial.println(c);
    switch (c)
    {
      case 'f':
        stepMotor(FORWARD);
        break;
      case 'b':
        stepMotor(BACKWARD);
        break;
      case 'p':
        gMotorStepDelay *= -1;
        break;
      case 'c':
        Serial.println("Entering sCmd.readSerial()");
        sCmd.readSerial();
        Serial.println("Exited sCmd.readSerial()");
        break;
      case 'l':
        gLogLevel = (gLogLevel + 1) % (LOG_LEVEL_VERBOSE + 1);
        //Log.Init(9600, gLogLevel);
        static const  char* levelStrings[]  = {"DISABLED","VERBOSE", "DEBUG", "INFO", "ERROR"};
        Serial.print(F("Log level is ")); Serial.println(levelStrings[gLogLevel]);
      case 'm':
        gMute = !gMute;
        break;
      case 'i':
        printInfo();
        break;
      case '\n':
        break; // ignore newlines
      default:
        Serial.print(F("Ignorning unknown command: ")); Serial.println(c);        
    }
  }
}

void loop()
{
  static int iNote = 0;
  static int motorDirection = FORWARD;

  waitForNote(iNote, gMotorStepDelay);
  checkSonar();
  checkButtons();

  if (gMotorStepDelay > 0) 
  {
    stepMotor(motorDirection);
    if (motorDirection == FORWARD)
    {
      iNote++;
      if (iNote == (gNoteCount-1))
      {
        motorDirection = BACKWARD;
      }
    }
    else
    { 
      iNote--;
      if (iNote == 0)
      {
        motorDirection = FORWARD;
      }
    }
  }
}

void shutdown ()
{
  if (gGinsing == true)
  {
     gGS.end();
  }
}


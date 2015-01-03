/**
 ******************************************************************************
 * @file    application.cpp
 * @authors  Satish Nair, Zachary Crockett and Mohit Bhoite
 * @version V1.0.0
 * @date    05-November-2013
 * @brief   Tinker application
 ******************************************************************************
  Copyright (c) 2013 Spark Labs, Inc.  All rights reserved.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program; if not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************
 */

/*
 * Brief of Drinkbot
 *
 * State Machine:
 *
 *                   BOOTING
 *                      |
 *                      v
 *   (collect data) LISTENING <-+
 *        API called? Y |       |
 *                      v       |
 *                    START     |
 *                      |       | finished? Y
 *                      v       |
 *              +--> PUMPING    |
 *  finished? N |       |       |
 *              +-------+-------+
 *                      |
 *                      v
 *                     end (never reach here)
 */

/* Includes ------------------------------------------------------------------*/
#include "application.h"

/* Definitions ---------------------------------------------------------------*/
SYSTEM_MODE(AUTOMATIC);

enum {
	STATE_BOOTING = 0,
	STATE_LISTENING = 1,
	STATE_START = 2,
	STATE_PUMPING = 3,
	STATE_MAX,
};

#define NPUMPS		8
#define vol2time(vol)	((unsigned long)vol*1000)	// vol -> millis, to be fixed according to actual case

/* Variables -----------------------------------------------------------------*/
static double temperature = 0.0;
static double alcohol1 = 0.0;
static double alcohol2 = 0.0;
static int state = STATE_BOOTING;
static int pumpsOnVol[NPUMPS] = {0, 0, 0, 0, 0, 0, 0, 0};	// unit: cl
static int pumpsStatus[NPUMPS] = {0, 0, 0, 0, 0, 0, 0, 0};	// 1: on, 0: off
static unsigned long timeStart = 0;

static const int pump2io[NPUMPS] = {D0, D1, A0, A1, A4, A5, A6, A7};

/* Function prototypes -------------------------------------------------------*/
int tinkerDigitalRead(String pin);
int tinkerDigitalWrite(String command);
int tinkerAnalogRead(String pin);
int tinkerAnalogWrite(String command);
int drinkbotSetPumps(String command);
int drinkbotDebug(String command);

static void changestate(int s);
static int parseparams(String command, int id);
static int collectdata(void);
static void controlpump(int id, boolean on);
static void startpumpjobs(int p[NPUMPS]);
static int npumpson(int ps[NPUMPS]);
static void finishpumpjobs(int p[NPUMPS]);

/* This function is called once at start up ----------------------------------*/
void setup()
{
	int i;

	//Setup the logging functions
	Serial.begin(9600);

	//Setup communication bus with secondary processor
//	Serial1.begin(9600);

	//Setup the Tinker application here
	RGB.brightness(12);

	//Register all the Tinker functions
	Spark.function("digitalread", tinkerDigitalRead);
	Spark.function("digitalwrite", tinkerDigitalWrite);

	Spark.function("analogread", tinkerAnalogRead);
	Spark.function("analogwrite", tinkerAnalogWrite);

	//Register all the Drinkbot functions
	Spark.function("setpumps", drinkbotSetPumps);
	Spark.function("debug", drinkbotDebug);

	//Register all the Drinkbot variables
	Spark.variable("temperature", &temperature, DOUBLE);
	pinMode(D7, INPUT);
	Spark.variable("alcohol1", &alcohol1, DOUBLE);
	pinMode(A3, INPUT);
	Spark.variable("alcohol2", &alcohol2, DOUBLE);
	pinMode(A2, INPUT);

	//Pumps
	for (i = 0; i < NPUMPS; i++) {
		pinMode(pump2io[i], OUTPUT);
		digitalWrite(pump2io[i], LOW);
	}

	//RGB LED
	pinMode(D7, OUTPUT);
}

/* This function loops forever --------------------------------------------*/
void loop()
{
	switch (state) {
	case STATE_BOOTING:
		Serial.println("Booting...");

		temperature = 0.0;
		alcohol1 = 0.0;
		alcohol2 = 0.0;
		state = STATE_BOOTING;
		memset(pumpsOnVol, 0, sizeof(pumpsOnVol));
		memset(pumpsStatus, 0, sizeof(pumpsStatus));
		timeStart = 0;

		changestate(STATE_LISTENING);
		break;
	case STATE_LISTENING:
		/*
		 * sleep here awaiting for remote api calls,
		 * or collect data from sensors
		 */
		collectdata();
		break;
	case STATE_START:
		startpumpjobs(pumpsOnVol);
		changestate(STATE_PUMPING);
		break;
	case STATE_PUMPING:
		finishpumpjobs(pumpsOnVol);
		break;
	default:
		Serial.print("no such state: ");
		Serial.println(state);
		break;
	}
}

/*******************************************************************************
 * Function Name  : tinkerDigitalRead
 * Description    : Reads the digital value of a given pin
 * Input          : Pin
 * Output         : None.
 * Return         : Value of the pin (0 or 1) in INT type
                    Returns a negative number on failure
 *******************************************************************************/
int tinkerDigitalRead(String pin)
{
	//convert ascii to integer
	int pinNumber = pin.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber < 0 || pinNumber > 7)
		return -1;

	if(pin.startsWith("D"))
	{
		pinMode(pinNumber, INPUT_PULLDOWN);
		return digitalRead(pinNumber);
	}
	else if (pin.startsWith("A"))
	{
		pinMode(pinNumber + 10, INPUT_PULLDOWN);
		return digitalRead(pinNumber + 10);
	}
	return -2;
}

/*******************************************************************************
 * Function Name  : tinkerDigitalWrite
 * Description    : Sets the specified pin HIGH or LOW
 * Input          : Pin and value
 * Output         : None.
 * Return         : 1 on success and a negative number on failure
 *******************************************************************************/
int tinkerDigitalWrite(String command)
{
	bool value = 0;
	//convert ascii to integer
	int pinNumber = command.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber < 0 || pinNumber > 7) return -1;

	if(command.substring(3, 7) == "HIGH") value = 1;
	else if(command.substring(3, 6) == "LOW") value = 0;
	else return -2;

	if(command.startsWith("D"))
	{
		pinMode(pinNumber, OUTPUT);
		digitalWrite(pinNumber, value);
		return 1;
	}
	else if(command.startsWith("A"))
	{
		pinMode(pinNumber + 10, OUTPUT);
		digitalWrite(pinNumber + 10, value);
		return 1;
	}
	else return -3;
}

/*******************************************************************************
 * Function Name  : tinkerAnalogRead
 * Description    : Reads the analog value of a pin
 * Input          : Pin
 * Output         : None.
 * Return         : Returns the analog value in INT type (0 to 4095)
                    Returns a negative number on failure
 *******************************************************************************/
int tinkerAnalogRead(String pin)
{
	//convert ascii to integer
	int pinNumber = pin.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber < 0 || pinNumber > 7) return -1;

	if(pin.startsWith("D"))
	{
		pinMode(pinNumber, INPUT);
		return analogRead(pinNumber);
	}
	else if (pin.startsWith("A"))
	{
		pinMode(pinNumber + 10, INPUT);
		return analogRead(pinNumber + 10);
	}
	return -2;
}

/*******************************************************************************
 * Function Name  : tinkerAnalogWrite
 * Description    : Writes an analog value (PWM) to the specified pin
 * Input          : Pin and Value (0 to 255)
 * Output         : None.
 * Return         : 1 on success and a negative number on failure
 *******************************************************************************/
int tinkerAnalogWrite(String command)
{
	//convert ascii to integer
	int pinNumber = command.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber < 0 || pinNumber > 7) return -1;

	String value = command.substring(3);

	if(command.startsWith("D"))
	{
		pinMode(pinNumber, OUTPUT);
		analogWrite(pinNumber, value.toInt());
		return 1;
	}
	else if(command.startsWith("A"))
	{
		pinMode(pinNumber + 10, OUTPUT);
		analogWrite(pinNumber + 10, value.toInt());
		return 1;
	}
	else return -2;
}

/*******************************************************************************
 * Function Name  : drinkbotSetPumps
 * Description    : Setup and start the pumping jobs,
 *                  e.g. P0:xx;P1:xx;P2:xx;P3:xx;P4:xx;P5:xx;P6:xx;P7:xx
 *                  where P0 ~ P7 are the 8 pumps and xx is the cl value.
 * Input          : pumps id and corresponding cl value string
 * Output         : None.
 * Return         : 0 for success, others for error numbers
 *******************************************************************************/
int drinkbotSetPumps(String command)
{
	int i;

	Serial.print(__func__);
	Serial.print(", args: ");
	Serial.println(command);

	if (command.length() != 47) {
//		Serial.println("Please use correct format for args, e.g.");
//		Serial.println("\tP0:10;P1:00;P2:20;P3:05;P4:07;P5:15;P6:00;P7:10");
		return -EINVAL;
	}

	if (state != STATE_LISTENING) {
		return -EBUSY;
	}

	/* parse command */
	Serial.print("pumpsOnVol: ");
	for (i = 0; i < NPUMPS; i++) {
		pumpsOnVol[i] = parseparams(command, i);
		Serial.print(pumpsOnVol[i]);
		Serial.print(' ');
	}
	Serial.println("");

	changestate(STATE_START);
	return 0;
}

/*******************************************************************************
 * Function Name  : drinkbotDebug
 * Description    : Debugging purpose
 * Input          : Random things
 * Output         : None
 * Return         : The character ascii code it received
 *******************************************************************************/
int drinkbotDebug(String command)
{
	int i;

	Serial.print(__func__);
	Serial.print(", args: ");
	Serial.println(command);

	if (command.equals("reset")) {
		Serial.println("Resetting Drinkbot...");
		for (i = 0; i < NPUMPS; i++)
			controlpump(i, false);
		changestate(STATE_BOOTING);
	}


	Serial.println("dump all variables:");

	Serial.print("temperature: ");
	Serial.println(temperature);

	Serial.print("alcohol1: ");
	Serial.println(alcohol1);

	Serial.print("alcohol2: ");
	Serial.println(alcohol2);

	Serial.print("state: ");
	Serial.println(state);

	Serial.print("timeStart: ");
	Serial.println(timeStart);

	Serial.print("pumpsOnVol: ");
	for (i = 0; i < NPUMPS; i++) {
		Serial.print(pumpsOnVol[i]);
		Serial.print(' ');
	}
	Serial.println("");

	Serial.print("pumpsStatus: ");
	for (i = 0; i < NPUMPS; i++) {
		Serial.print(pumpsStatus[i]);
		Serial.print(' ');
	}
	Serial.println("");

	return command.charAt(0);
}


static void changestate(int s)
{
	if (s < 0 || s >= STATE_MAX)
		Serial.print("change state failed: ");
	else
		Serial.print("change state: ");
	Serial.print(state);
	Serial.print(" -> ");
	Serial.println(s);
	state = s;
}

// the parameters should follow below format:
//     P0:XX;P1:XX;P2:XX;P3:XX;P4:XX;P5:XX;P6:XX;P7:XX
static int parseparams(String command, int id)
{
	return command.substring(6*id+3, 6*id+5).toInt();
}

// TODO need to poll sensors and calculate data
static int collectdata(void)
{
	int reading = 0;

	reading = analogRead(A3);
	alcohol1 = reading;
	reading = analogRead(A2);
	alcohol2 = reading;
	reading = analogRead(D7);
	temperature = reading;

	return 0;
}

static void controlpump(int id, boolean on)
{
	if (id < 0 || id > NPUMPS) {
		Serial.print("invalid pump id: ");
		Serial.println(id);
		id = constrain(id, 0, (NPUMPS-1));
	}

	pinMode(pump2io[id], OUTPUT);
	if (on) {
		digitalWrite(pump2io[id], HIGH);
		pumpsStatus[id] = 1;
	} else {
		digitalWrite(pump2io[id], LOW);
		pumpsStatus[id] = 0;
	}
}

static void startpumpjobs(int p[NPUMPS])
{
	int i;

	timeStart = millis();
	Serial.print("timeStart: ");
	Serial.println(timeStart);

	Serial.print("starting pumps: ");
	for (i = 0; i < NPUMPS; i++) {
		if (p[i] != 0) {
			controlpump(i, true);
			Serial.print(i);
			Serial.print(' ');
		}
	}
	Serial.println("");
}

static int npumpson(int ps[NPUMPS])
{
	int i, n = 0;
	for (i = 0; i < NPUMPS; i++)
		n += ps[i];
	return n;
}

static void finishpumpjobs(int p[NPUMPS])
{
	int i;
	unsigned long timeDelta;

	// FIXME
	// support up to about 49 days for an unsigned long millis
	// better to wrap around
	timeDelta = millis() - timeStart;

	for (i = 0; i < NPUMPS; i++) {
		if (timeDelta > vol2time(p[i]) &&
		    pumpsStatus[i] == 1) {
			Serial.print("stoping pump: ");
			Serial.print(i);
			Serial.print(" in ");
			Serial.print(timeDelta);
			Serial.println("ms");
			controlpump(i, false);
		}
	}

	if (npumpson(pumpsStatus) == 0) {
		Serial.print("All pumping jobs finished, time elapsed: ");
		Serial.println(timeDelta);
		changestate(STATE_LISTENING);
	}
}
/*
  xdrv_06_wb.ino - wirenboard support for Sonoff-Tasmota

  Copyright (C) 2018  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_WB

#include "WBDevice.h"

#ifdef USE_DOMOTICZ
#error Cannot use Domoticz and Wirenboard in one configuration
#endif

#ifdef USE_WEBSERVER
const char HTTP_FORM_WB[] PROGMEM =
  "<fieldset><legend><b>&nbsp;" D_WB_PARAMETERS "&nbsp;</b></legend><form method='post' action='sv'>"
  "<input id='w' name='w' value='4' hidden><input id='r' name='r' value='1' hidden>"
  "<br/><table>";
const char HTTP_FORM_WB_RELAY[] PROGMEM =
  "<tr><td style='width:260px'><b>" D_WB_CURTAIN " {1</b></td><td style='width:70px'><input id='r{1' name='r{1' placeholder='0' value='{2'></td></tr>";
#endif  // USE_WEBSERVER

enum WbCommands { CMND_CURTAIN };
const char kWbCommands[] PROGMEM = D_CMND_CURTAIN "|" D_CMND_KEYIDX;

CMqttDevice *SonoffDevice = NULL;

void WirenboardMqttUpdate()
{
  if (!SonoffDevice) return;

  for (byte i = Settings.param[P_CURTAIN]?2:0; i < devices_present; i++) {
    SonoffDevice->Set(i, bitRead(power, i));
  }
  snprintf_P(log_data, sizeof(log_data), PSTR("%04d-%02d-%02d %02d:%02d"),
    RtcTime.year, RtcTime.month, RtcTime.day_of_month, RtcTime.hour, RtcTime.minute);
  SonoffDevice->Set("Status", log_data);
  SonoffDevice->Publish();
}

void WirenboardMqttSubscribe()
{
  if (!SonoffDevice){
    static const char Names[][3] = {"K1", "K2", "K3", "K4"};
    
    int devices = devices_present>4?4:devices_present;
    if (Settings.param[P_CURTAIN])
    {
      CMqttControl *controls = new CMqttControl[devices+2];
      controls[0].SetType("Up", CMqttControl::PushButton);
      controls[1].SetType("Down", CMqttControl::PushButton);
      controls[2].SetType("Stop", CMqttControl::PushButton);
      for (int i=2;i<devices;i++) controls[i+1].SetType(Names[i], CMqttControl::Switch);
      controls[devices+1].SetType("Status", CMqttControl::Text, true);
      SonoffDevice = new CMqttDevice(&MqttClient, my_hostname, Settings.friendlyname[0],
                  devices+2, controls);
    } else {
      CMqttControl *controls = new CMqttControl[devices+1];
      for (int i=0;i<devices;i++) controls[i].SetType(Names[i], CMqttControl::Switch);
      controls[devices].SetType("Status", CMqttControl::Text, true);
      SonoffDevice = new CMqttDevice(&MqttClient, my_hostname, Settings.friendlyname[0],
                  devices+1, controls);
    }
    SonoffDevice->Create();           
  }

  SonoffDevice->Subscribe();           
  SonoffDevice->Publish();   
  WirenboardMqttUpdate();        
}

boolean WirenboardMqttData()
{
  char stemp1[10];
  unsigned long idx = 0;
  int16_t nvalue;
  int16_t found = 0;

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_DOMOTICZ D_RECEIVED_TOPIC " %s, " D_DATA " %s"), XdrvMailbox.topic, XdrvMailbox.data);
  AddLog(LOG_LEVEL_DEBUG_MORE);

  CMqttControl* control = SonoffDevice->GetControlFromTopic(XdrvMailbox.topic);
  if (control)
  {
    if (Settings.param[P_CURTAIN]) {
      Settings.pulse_timer[0] = 600;
      Settings.pulse_timer[1] = 600;
    }

    int val = XdrvMailbox.data>0?atoi(XdrvMailbox.data):0;
    snprintf_P(log_data, sizeof(log_data), PSTR("WB: data from control %s"), control->Name);
    AddLog(LOG_LEVEL_DEBUG);
    if (control->Name=="Up")
    {
      ExecuteCommandPower(1, POWER_ON);
      ExecuteCommandPower(2, POWER_OFF);
    }
    else if (control->Name=="Down")
    {
      ExecuteCommandPower(1, POWER_OFF);
      ExecuteCommandPower(2, POWER_ON);
    }
    else if (control->Name=="Stop")
    {
      ExecuteCommandPower(1, POWER_OFF);
      ExecuteCommandPower(2, POWER_ON);
    }
    else if (control->Name[0]=='K')
    {
      ExecuteCommandPower(atoi(control->Name+1), val?POWER_ON:POWER_OFF);
    }
    else
      return false;

    return true;
  }  

  return 0;
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

boolean WirenboardCommand()
{
  char stemp1[10];
  snprintf_P(log_data, sizeof(log_data), PSTR("WB: Receive topic %s, data %s"), XdrvMailbox.topic, XdrvMailbox.data);
  AddLog(LOG_LEVEL_DEBUG_MORE);
  
  char command [CMDSZ];
  boolean serviced = true;
  uint8_t dmtcz_len = strlen(D_CMND_WB);  // Prep for string length change

  if (!strncasecmp_P(XdrvMailbox.topic, PSTR(D_CMND_WB), dmtcz_len)) {  // Prefix
    snprintf_P(log_data, sizeof(log_data), PSTR("Got preffix"));
    AddLog(LOG_LEVEL_DEBUG);
    int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic +dmtcz_len, kWbCommands);
    if (CMND_CURTAIN == command_code) {
      snprintf_P(log_data, sizeof(log_data), PSTR("Got command curtain"));
      AddLog(LOG_LEVEL_DEBUG);
      if (XdrvMailbox.payload >= 0) {
        Settings.param[P_CURTAIN] = (char)XdrvMailbox.payload;
        restart_flag = 2;
      }
      snprintf_P(log_data, sizeof(log_data), PSTR("Reboot?"));
      AddLog(LOG_LEVEL_DEBUG);
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("Cmd: %s, I: %d"), command, XdrvMailbox.index);
    }
    else serviced = false;
  }
  else serviced = false;

  return serviced;
}

boolean WirenboardButton(byte key, byte device, byte state, byte svalflg)
{
  /*
  if ((Settings.domoticz_key_idx[device -1] || Settings.domoticz_switch_idx[device -1]) && (svalflg)) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"command\":\"switchlight\",\"idx\":%d,\"switchcmd\":\"%s\"}"),
      (key) ? Settings.domoticz_switch_idx[device -1] : Settings.domoticz_key_idx[device -1], (state) ? (2 == state) ? "Toggle" : "On" : "Off");
    MqttPublish(domoticz_in_topic);
    return 1;
  } else {
    return 0;
  }
  */
 return 0;
}

/*********************************************************************************************\
 * Sensors
 *
 * Source : https://www.domoticz.com/wiki/Domoticz_API/JSON_URL%27s
 *          https://www.domoticz.com/wiki/MQTT
 *
 * Percentage, Barometric, Air Quality:
 * {\"idx\":%d,\"nvalue\":%s}, Idx, Value
 *
 * Humidity:
 * {\"idx\":%d,\"nvalue\":%s,\"svalue\":\"%s\"}, Idx, Humidity, HumidityStatus
 *
 * All other:
 * {\"idx\":%d,\"nvalue\":0,\"svalue\":\"%s\"}, Idx, Value(s)
 *
\*********************************************************************************************/
/*
uint8_t WirenboardHumidityState(char *hum)
{
  uint8_t h = atoi(hum);
  return (!h) ? 0 : (h < 40) ? 2 : (h > 70) ? 3 : 1;
}

void WirenboardSensor(byte idx, char *data)
{
  if (Settings.domoticz_sensor_idx[idx]) {
    char dmess[64];

    memcpy(dmess, mqtt_data, sizeof(dmess));
    if (DZ_AIRQUALITY == idx) {
      snprintf_P(mqtt_data, sizeof(dmess), PSTR("{\"idx\":%d,\"nvalue\":%s}"), Settings.domoticz_sensor_idx[idx], data);
    } else {
      snprintf_P(mqtt_data, sizeof(dmess), PSTR("{\"idx\":%d,\"nvalue\":0,\"svalue\":\"%s\"}"), Settings.domoticz_sensor_idx[idx], data);
    }
    MqttPublish(domoticz_in_topic);
    memcpy(mqtt_data, dmess, sizeof(dmess));
  }
}

void WirenboardSensor(byte idx, uint32_t value)
{
  char data[16];
  snprintf_P(data, sizeof(data), PSTR("%d"), value);
  DomoticzSensor(idx, data);
}

void WirenboardTempHumSensor(char *temp, char *hum)
{
  char data[16];
  snprintf_P(data, sizeof(data), PSTR("%s;%s;%d"), temp, hum, DomoticzHumidityState(hum));
  DomoticzSensor(DZ_TEMP_HUM, data);
}

void WirenboardTempHumPressureSensor(char *temp, char *hum, char *baro)
{
  char data[32];
  snprintf_P(data, sizeof(data), PSTR("%s;%s;%d;%s;5"), temp, hum, DomoticzHumidityState(hum), baro);
  DomoticzSensor(DZ_TEMP_HUM_BARO, data);
}

void WirenboardSensorPowerEnergy(uint16_t power, char *energy)
{
  char data[16];
  snprintf_P(data, sizeof(data), PSTR("%d;%s"), power, energy);
  DomoticzSensor(DZ_POWER_ENERGY, data);
}
*/
/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

#ifdef USE_WEBSERVER
const char S_CONFIGURE_WIRENBOARD[] PROGMEM = D_CONFIGURE_WIRENBOARD;

void HandleWirenboardConfiguration()
{
  if (HTTP_USER == webserver_state) {
    HandleRoot();
    return;
  }
  AddLog_P(LOG_LEVEL_DEBUG, S_LOG_HTTP, S_CONFIGURE_WIRENBOARD);

  char stemp[32];

  String page = FPSTR(HTTP_HEAD);
  page.replace(F("{v}"), FPSTR(S_CONFIGURE_WIRENBOARD));
  page += FPSTR(HTTP_HEAD_STYLE);
  page += FPSTR(HTTP_FORM_WB);
  /*
  for (int i = 0; i < MAX_WB_IDX; i++) {
    if (i < devices_present) {
      page += FPSTR(HTTP_FORM_WB_RELAY);
      page.replace("{2", String((int)Settings.domoticz_relay_idx[i]));
      page.replace("{3", String((int)Settings.domoticz_key_idx[i]));
    }
    if (pin[GPIO_SWT1 +i] < 99) {
      page += FPSTR(HTTP_FORM_WB_SWITCH);
      page.replace("{4", String((int)Settings.domoticz_switch_idx[i]));
    }
    page.replace("{1", String(i +1));
  }*/
  /*
  for (int i = 0; i < DZ_MAX_SENSORS; i++) {
    page += FPSTR(HTTP_FORM_DOMOTICZ_SENSOR);
    page.replace("{1", String(i +1));
    page.replace("{2", GetTextIndexed(stemp, sizeof(stemp), i, kDomoticzSensors));
    page.replace("{5", String((int)Settings.domoticz_sensor_idx[i]));
  }
  page += FPSTR(HTTP_FORM_DOMOTICZ_TIMER);
  page.replace("{6", String((int)Settings.domoticz_update_timer));
  */
  page += F("</table>");
  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_BTN_CONF);
  ShowPage(page);
}

void DomoticzSaveSettings()
{
  char stemp[20];
  char ssensor_indices[6 * MAX_DOMOTICZ_SNS_IDX];
  char tmp[100];

  for (byte i = 0; i < MAX_DOMOTICZ_IDX; i++) {
    snprintf_P(stemp, sizeof(stemp), PSTR("r%d"), i +1);
    WebGetArg(stemp, tmp, sizeof(tmp));
    Settings.domoticz_relay_idx[i] = (!strlen(tmp)) ? 0 : atoi(tmp);
    snprintf_P(stemp, sizeof(stemp), PSTR("k%d"), i +1);
    WebGetArg(stemp, tmp, sizeof(tmp));
    Settings.domoticz_key_idx[i] = (!strlen(tmp)) ? 0 : atoi(tmp);
    snprintf_P(stemp, sizeof(stemp), PSTR("s%d"), i +1);
    WebGetArg(stemp, tmp, sizeof(tmp));
    Settings.domoticz_switch_idx[i] = (!strlen(tmp)) ? 0 : atoi(tmp);
  }
  ssensor_indices[0] = '\0';
  for (byte i = 0; i < DZ_MAX_SENSORS; i++) {
    snprintf_P(stemp, sizeof(stemp), PSTR("l%d"), i +1);
    WebGetArg(stemp, tmp, sizeof(tmp));
    Settings.domoticz_sensor_idx[i] = (!strlen(tmp)) ? 0 : atoi(tmp);
    snprintf_P(ssensor_indices, sizeof(ssensor_indices), PSTR("%s%s%d"), ssensor_indices, (strlen(ssensor_indices)) ? "," : "",  Settings.domoticz_sensor_idx[i]);
  }
  WebGetArg("ut", tmp, sizeof(tmp));
  Settings.domoticz_update_timer = (!strlen(tmp)) ? DOMOTICZ_UPDATE_TIMER : atoi(tmp);

  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_DOMOTICZ D_CMND_IDX " %d,%d,%d,%d, " D_CMND_KEYIDX " %d,%d,%d,%d, " D_CMND_SWITCHIDX " %d,%d,%d,%d, " D_CMND_SENSORIDX " %s, " D_CMND_UPDATETIMER " %d"),
    Settings.domoticz_relay_idx[0], Settings.domoticz_relay_idx[1], Settings.domoticz_relay_idx[2], Settings.domoticz_relay_idx[3],
    Settings.domoticz_key_idx[0], Settings.domoticz_key_idx[1], Settings.domoticz_key_idx[2], Settings.domoticz_key_idx[3],
    Settings.domoticz_switch_idx[0], Settings.domoticz_switch_idx[1], Settings.domoticz_switch_idx[2], Settings.domoticz_switch_idx[3],
    ssensor_indices, Settings.domoticz_update_timer);
  AddLog(LOG_LEVEL_INFO);
}
#endif  // USE_WEBSERVER

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

#define XDRV_06

boolean Xdrv06(byte function)
{
  boolean result = false;

  if (Settings.flag.mqtt_enabled) {
    switch (function) {
      case FUNC_COMMAND:
        result = WirenboardCommand();
        break;
      case FUNC_MQTT_SUBSCRIBE:
        WirenboardMqttSubscribe();
        break;
      case FUNC_MQTT_INIT:
        break;
      case FUNC_MQTT_DATA:
        result = WirenboardMqttData();
        break;
      case FUNC_EVERY_SECOND:
        WirenboardMqttUpdate();
        break;
      case FUNC_SHOW_SENSOR:
//        DomoticzSendSensor();
        break;
    }
  }
  return result;
}

#endif  // USE_DOMOTICZ

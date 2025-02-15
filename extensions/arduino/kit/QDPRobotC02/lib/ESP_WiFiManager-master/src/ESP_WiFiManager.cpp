/****************************************************************************************************************************
   ESP_WiFiManager.cpp
   For ESP8266 / ESP32 boards

   ESP_WiFiManager is a library for the ESP8266/Arduino platform
   (https://github.com/esp8266/Arduino) to enable easy
   configuration and reconfiguration of WiFi credentials using a Captive Portal
   inspired by:
   http://www.esp8266.com/viewtopic.php?f=29&t=2520
   https://github.com/chriscook8/esp-arduino-apboot
   https://github.com/esp8266/Arduino/blob/master/libraries/DNSServer/examples/CaptivePortalAdvanced/

   Forked from Tzapu https://github.com/tzapu/WiFiManager
   and from Ken Taylor https://github.com/kentaylor

   Built by Khoi Hoang https://github.com/khoih-prog/ESP_WiFiManager
   Licensed under MIT license
   Version: 1.0.6

   Version Modified By   Date      Comments
   ------- -----------  ---------- -----------
    1.0.0   K Hoang      07/10/2019 Initial coding
    1.0.1   K Hoang      13/12/2019 Fix bug. Add features. Add support for ESP32
    1.0.2   K Hoang      19/12/2019 Fix bug that keeps ConfigPortal in endless loop if Portal/Router SSID or Password is NULL.
    1.0.3   K Hoang	 05/01/2020 Option not displaying AvailablePages in Info page.
    1.0.4   K Hoang	 07/01/2020 Add RFC952 setHostname feature.
    1.0.5   K Hoang	 15/01/2020 Add configurable DNS feature. Thanks to @Amorphous of https://community.blynk.cc
    1.0.6   K Hoang      03/02/2020 Add support for ArduinoJson version 6.0.0+ ( tested with v6.14.1 )
 *****************************************************************************************************************************/

#include "ESP_WiFiManager.h"

#define DEBUG_WIFIMGR			false

ESP_WMParameter::ESP_WMParameter(const char *custom)
{
  _id = NULL;
  _placeholder = NULL;
  _length = 0;
  _value = NULL;
  _labelPlacement = WFM_LABEL_BEFORE;

  _customHTML = custom;
}

ESP_WMParameter::ESP_WMParameter(const char *id, const char *placeholder, const char *defaultValue, int length)
{
  init(id, placeholder, defaultValue, length, "", WFM_LABEL_BEFORE);
}

ESP_WMParameter::ESP_WMParameter(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom)
{
  init(id, placeholder, defaultValue, length, custom, WFM_LABEL_BEFORE);
}

ESP_WMParameter::ESP_WMParameter(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom, int labelPlacement)
{
  init(id, placeholder, defaultValue, length, custom, labelPlacement);
}

void ESP_WMParameter::init(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom, int labelPlacement)
{
  _id = id;
  _placeholder = placeholder;
  _length = length;
  _labelPlacement = labelPlacement;

  _value = new char[_length + 1];

  if (_value != NULL)
  {
    memset(_value, 0, _length + 1);

    if (defaultValue != NULL)
    {
      strncpy(_value, defaultValue, _length);
    }
  }
  _customHTML = custom;
}

ESP_WMParameter::~ESP_WMParameter()
{
  if (_value != NULL)
  {
    delete[] _value;
  }
}

const char* ESP_WMParameter::getValue()
{
  return _value;
}

const char* ESP_WMParameter::getID()
{
  return _id;
}
const char* ESP_WMParameter::getPlaceholder()
{
  return _placeholder;
}

int ESP_WMParameter::getValueLength()
{
  return _length;
}

int ESP_WMParameter::getLabelPlacement()
{
  return _labelPlacement;
}
const char* ESP_WMParameter::getCustomHTML()
{
  return _customHTML;
}

/**
   [getParameters description]
   @access public
*/
ESP_WMParameter** ESP_WiFiManager::getParameters() {
  return _params;
}

/**
   [getParametersCount description]
   @access public
*/
int ESP_WiFiManager::getParametersCount() {
  return _paramsCount;
}

char* ESP_WiFiManager::getRFC952_hostname(const char* iHostname)
{
  memset(RFC952_hostname, 0, sizeof(RFC952_hostname));

  size_t len = (RFC952_HOSTNAME_MAXLEN < strlen(iHostname)) ? RFC952_HOSTNAME_MAXLEN : strlen(iHostname);

  size_t j = 0;

  for (size_t i = 0; i < len - 1; i++)
  {
    if (isalnum(iHostname[i]) || iHostname[i] == '-')
    {
      RFC952_hostname[j] = iHostname[i];
      j++;
    }
  }
  // no '-' as last char
  if (isalnum(iHostname[len - 1]) || (iHostname[len - 1] != '-'))
    RFC952_hostname[j] = iHostname[len - 1];

  return RFC952_hostname;
}

ESP_WiFiManager::ESP_WiFiManager(const char *iHostname)
{
#if USE_DYNAMIC_PARAMS
  _max_params = WIFI_MANAGER_MAX_PARAMS;
  _params = (ESP_WMParameter**)malloc(_max_params * sizeof(ESP_WMParameter*));
#endif

  //WiFi not yet started here, must call WiFi.mode(WIFI_STA) and modify function WiFiGenericClass::mode(wifi_mode_t m) !!!

  WiFi.mode(WIFI_STA);

  if (iHostname[0] == 0)
  {
#ifdef ESP8266
    String _hostname = "ESP8266-" + String(ESP.getChipId(), HEX);
#else		//ESP32
    String _hostname = "ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
#endif
    _hostname.toUpperCase();

    getRFC952_hostname(_hostname.c_str());

  }
  else
  {
    // Prepare and store the hostname only not NULL
    getRFC952_hostname(iHostname);
  }

  DEBUG_WM(F("RFC925 Hostname = "));
  DEBUG_WM(RFC952_hostname);

  setHostname();

  networkIndices = NULL;
}

ESP_WiFiManager::~ESP_WiFiManager()
{
#if USE_DYNAMIC_PARAMS
  if (_params != NULL)
  {
#if DEBUG_WIFIMGR
    DEBUG_WM(F("freeing allocated params!"));
#endif

    free(_params);
  }
#endif

#if DEBUG_WIFIMGR
  Serial.printf("ESP_WiFiManager::~ESP_WiFiManager : networkIndices = %08X, networkIndicesptr = %08X\n", networkIndices, &networkIndices);
#endif

  if (networkIndices)
  {
    free(networkIndices); //indices array no longer required so free memory
  }
}

#if USE_DYNAMIC_PARAMS
bool ESP_WiFiManager::addParameter(ESP_WMParameter *p)
#else
void ESP_WiFiManager::addParameter(ESP_WMParameter *p)
#endif
{
#if USE_DYNAMIC_PARAMS

  if (_paramsCount == _max_params)
  {
    // rezise the params array
    _max_params += WIFI_MANAGER_MAX_PARAMS;
    DEBUG_WM(F("Increasing _max_params to:"));
    DEBUG_WM(_max_params);
    ESP_WMParameter** new_params = (ESP_WMParameter**)realloc(_params, _max_params * sizeof(ESP_WMParameter*));

    if (new_params != NULL)
    {
      _params = new_params;
    }
    else
    {
      DEBUG_WM(F("ERROR: failed to realloc params, size not increased!"));
      return false;
    }
  }

  _params[_paramsCount] = p;
  _paramsCount++;
  DEBUG_WM(F("Adding parameter"));
  DEBUG_WM(p->getID());
  return true;

#else

  // Danger here. Better to use Tzapu way here
  if (_paramsCount < (WIFI_MANAGER_MAX_PARAMS))
  {
    _params[_paramsCount] = p;
    _paramsCount++;
    DEBUG_WM("Adding parameter");
    DEBUG_WM(p->getID());
  }
  else
  {
    DEBUG_WM("Can't add parameter. Full");
  }

#endif
}

void ESP_WiFiManager::setupConfigPortal()
{
  stopConfigPortal = false; //Signal not to close config portal

  /*This library assumes autoconnect is set to 1. It usually is
    but just in case check the setting and turn on autoconnect if it is off.
    Some useful discussion at https://github.com/esp8266/Arduino/issues/1615*/
  if (WiFi.getAutoConnect() == 0)
    WiFi.setAutoConnect(1);

  dnsServer.reset(new DNSServer());

#ifdef ESP8266
  server.reset(new ESP8266WebServer(80));
#else		//ESP32
  server.reset(new WebServer(80));
#endif

  /* Setup the DNS server redirecting all the domains to the apIP */
  if (dnsServer)
  {
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());
  }

  DEBUG_WM(F(""));
  _configPortalStart = millis();

  DEBUG_WM(F("Configuring access point... "));
  DEBUG_WM(_apName);

  if (_apPassword != NULL)
  {
    if (strlen(_apPassword) < 8 || strlen(_apPassword) > 63)
    {
      // fail passphrase to short or long!
      DEBUG_WM(F("Invalid AccessPoint password. Ignoring"));
      _apPassword = NULL;
    }
    DEBUG_WM(_apPassword);
  }

  //optional soft ip config
  if (_ap_static_ip)
  {
    DEBUG_WM(F("Custom AP IP/GW/Subnet"));
    WiFi.softAPConfig(_ap_static_ip, _ap_static_gw, _ap_static_sn);
  }

  if (_apPassword != NULL)
  {
    WiFi.softAP(_apName, _apPassword);//password option
  }
  else
  {
    WiFi.softAP(_apName);
  }

  delay(500); // Without delay I've seen the IP address blank
  DEBUG_WM(F("AP IP address: "));
  DEBUG_WM(WiFi.softAPIP());

  /* Setup web pages: root, wifi config pages, SO captive portal detectors and not found. */
  server->on("/", std::bind(&ESP_WiFiManager::handleRoot, this));
  server->on("/wifi", std::bind(&ESP_WiFiManager::handleWifi, this));
  server->on("/wifisave", std::bind(&ESP_WiFiManager::handleWifiSave, this));
  server->on("/close", std::bind(&ESP_WiFiManager::handleServerClose, this));
  server->on("/i", std::bind(&ESP_WiFiManager::handleInfo, this));
  server->on("/r", std::bind(&ESP_WiFiManager::handleReset, this));
  server->on("/state", std::bind(&ESP_WiFiManager::handleState, this));
  server->on("/scan", std::bind(&ESP_WiFiManager::handleScan, this));
  server->onNotFound(std::bind(&ESP_WiFiManager::handleNotFound, this));
  server->begin(); // Web server start
  DEBUG_WM(F("HTTP server started"));

}

boolean ESP_WiFiManager::autoConnect()
{
#ifdef ESP8266
  String ssid = "ESP_" + String(ESP.getChipId());
#else		//ESP32
  String ssid = "ESP_" + String((uint32_t)ESP.getEfuseMac());
#endif

  return autoConnect(ssid.c_str(), NULL);
}

/* This is not very useful as there has been an assumption that device has to be
  told to connect but Wifi already does it's best to connect in background. Calling this
  method will block until WiFi connects. Sketch can avoid
  blocking call then use (WiFi.status()==WL_CONNECTED) test to see if connected yet.
  See some discussion at https://github.com/tzapu/WiFiManager/issues/68
*/
boolean ESP_WiFiManager::autoConnect(char const *apName, char const *apPassword)
{
  DEBUG_WM(F(""));
  DEBUG_WM(F("AutoConnect"));

  // device will attempt to connect by itself; wait 10 secs
  // to see if it succeeds and should it fail, fall back to AP
  WiFi.mode(WIFI_STA);
  unsigned long startedAt = millis();

  while (millis() - startedAt < 10000)
  {
    //delay(100);
    delay(200);

    if (WiFi.status() == WL_CONNECTED)
    {
      float waited = (millis() - startedAt);
      DEBUG_WM(F("After waiting "));
      DEBUG_WM(waited / 1000);
      DEBUG_WM(F(" secs, local ip: "));
      DEBUG_WM(WiFi.localIP());
      return true;
    }
  }

  return startConfigPortal(apName, apPassword);
}

boolean  ESP_WiFiManager::startConfigPortal()
{
#ifdef ESP8266
  String ssid = "ESP_" + String(ESP.getChipId());
#else		//ESP32
  String ssid = "ESP_" + String((uint32_t)ESP.getEfuseMac());
#endif
  ssid.toUpperCase();

  return startConfigPortal(ssid.c_str(), NULL);
}

boolean  ESP_WiFiManager::startConfigPortal(char const *apName, char const *apPassword)
{
  //setup AP
  int connRes = WiFi.waitForConnectResult();

#if DEBUG_WIFIMGR
  DEBUG_WM("WiFi.waitForConnectResult Done");
#endif

  if (connRes == WL_CONNECTED)
  {
#if DEBUG_WIFIMGR
    DEBUG_WM("SET AP_STA");
#endif
    WiFi.mode(WIFI_AP_STA); //Dual mode works fine if it is connected to WiFi
  }
  else
  {
#if DEBUG_WIFIMGR
    DEBUG_WM("SET AP");
#endif
    WiFi.mode(WIFI_AP); // Dual mode becomes flaky if not connected to a WiFi network.
    // When ESP8266 station is trying to find a target AP, it will scan on every channel,
    // that means ESP8266 station is changing its channel to scan. This makes the channel of ESP8266 softAP keep changing too..
    // So the connection may break. From http://bbs.espressif.com/viewtopic.php?t=671#p2531
  }

  _apName = apName;
  _apPassword = apPassword;

  //notify we entered AP mode
  if (_apcallback != NULL)
  {
    DEBUG_WM("_apcallback");
    _apcallback(this);
  }

  connect = false;

  setupConfigPortal();

  bool TimedOut = true;

#if DEBUG_WIFIMGR
  DEBUG_WM("ESP_WiFiManager::startConfigPortal : Enter loop");
#endif

  while (_configPortalTimeout == 0 || millis() < _configPortalStart + _configPortalTimeout)
  {
    //DNS
    dnsServer->processNextRequest();
    //HTTP
    server->handleClient();

    if (connect)
    {
      TimedOut = false;
      delay(2000);

      //DEBUG_WM(F("_configPortalTimeout ="));
      //DEBUG_WM(_configPortalTimeout);
      DEBUG_WM(F("Connecting to new AP"));

      // using user-provided  _ssid, _pass in place of system-stored ssid and pass
      if (connectWifi(_ssid, _pass) != WL_CONNECTED)
      {
        DEBUG_WM(F("Failed to connect."));
        WiFi.mode(WIFI_AP); // Dual mode becomes flaky if not connected to a WiFi network.
      }
      else
      {
        //notify that configuration has changed and any optional parameters should be saved
        if (_savecallback != NULL)
        {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }

      if (_shouldBreakAfterConfig)
      {
        //flag set to exit after config after trying to connect
        //notify that configuration has changed and any optional parameters should be saved
        if (_savecallback != NULL)
        {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }
    }

    if (stopConfigPortal)
    {
      DEBUG_WM("Stop ConfigPortal");  	//KH
      stopConfigPortal = false;
      break;
    }
    yield();
  }

  WiFi.mode(WIFI_STA);
  if (TimedOut)
  {
    setHostname();

    WiFi.begin();
    int connRes = waitForConnectResult();

#if DEBUG_WIFIMGR
    DEBUG_WM("Timed out connection result: ");
    DEBUG_WM(getStatus(connRes));
#endif
  }

  server->stop();
  server.reset();
  dnsServer->stop();
  dnsServer.reset();

  return  WiFi.status() == WL_CONNECTED;
}

int ESP_WiFiManager::connectWifi(String ssid, String pass)
{
  DEBUG_WM(F("Connecting wifi with new parameters..."));

  if (ssid != "")
  {
    resetSettings();

#if USE_CONFIGURABLE_DNS
    if (_sta_static_ip)
    {
      DEBUG_WM(F("Custom STA IP/GW/Subnet"));
      //***** Added section for DNS config option *****
      if (_sta_static_dns1 && _sta_static_dns2) {
        DEBUG_WM(F("dns1 and dns2 set"));
        WiFi.config(_sta_static_ip, _sta_static_gw, _sta_static_sn, _sta_static_dns1, _sta_static_dns2);
      }
      else if (_sta_static_dns1) {
        DEBUG_WM(F("only dns1 set"));
        WiFi.config(_sta_static_ip, _sta_static_gw, _sta_static_sn, _sta_static_dns1);
      }
      else {
        DEBUG_WM(F("No DNS server set"));
        WiFi.config(_sta_static_ip, _sta_static_gw, _sta_static_sn);
      }
      //***** End added section for DNS config option *****

      DEBUG_WM(WiFi.localIP());
    }
#else
    // check if we've got static_ip settings, if we do, use those.
    if (_sta_static_ip)
    {
      DEBUG_WM(F("Custom STA IP/GW/Subnet"));
      WiFi.config(_sta_static_ip, _sta_static_gw, _sta_static_sn);
      DEBUG_WM(WiFi.localIP());
    }
#endif

    //fix for auto connect racing issue
    if (WiFi.status() == WL_CONNECTED)
    {
      DEBUG_WM(F("Already connected. Bailing out."));
      return WL_CONNECTED;
    }

    WiFi.mode(WIFI_AP_STA); //It will start in station mode if it was previously in AP mode.

    setHostname();

    WiFi.begin(ssid.c_str(), pass.c_str());   // Start Wifi with new values.
  }
  else if (WiFi_SSID() == "")
  {
    DEBUG_WM(F("No saved credentials"));
  }

  int connRes = waitForConnectResult();
  DEBUG_WM("Connection result: ");
  DEBUG_WM(getStatus(connRes));

  //not connected, WPS enabled, no pass - first attempt
  if (_tryWPS && connRes != WL_CONNECTED && pass == "")
  {
    startWPS();
    //should be connected at the end of WPS
    connRes = waitForConnectResult();
  }

  return connRes;
}

uint8_t ESP_WiFiManager::waitForConnectResult()
{
  if (_connectTimeout == 0)
  {
#if DEBUG_WIFIMGR
    unsigned long startedAt = millis();
    DEBUG_WM(F("After waiting..."));
#endif

    int connRes = WiFi.waitForConnectResult();

#if DEBUG_WIFIMGR
    float waited = (millis() - startedAt);
    DEBUG_WM(waited / 1000);
    DEBUG_WM(F("seconds"));
#endif

    return connRes;
  }
  else
  {
    DEBUG_WM(F("Waiting for connection result with time out"));
    unsigned long start = millis();
    boolean keepConnecting = true;
    uint8_t status;

    while (keepConnecting)
    {
      status = WiFi.status();
      if (millis() > start + _connectTimeout)
      {
        keepConnecting = false;
        DEBUG_WM(F("Connection timed out"));
      }

      if (status == WL_CONNECTED || status == WL_CONNECT_FAILED)
      {
        keepConnecting = false;
      }
      delay(100);
    }
    return status;
  }
}

void ESP_WiFiManager::startWPS()
{
#ifdef ESP8266
  DEBUG_WM("START WPS");
  WiFi.beginWPSConfig();
  DEBUG_WM("END WPS");
#else		//ESP32
  // TODO
  DEBUG_WM("ESP32 WPS TODO");
#endif
}

//Convenient for debugging but wasteful of program space.
//Remove if short of space
const char* ESP_WiFiManager::getStatus(int status)
{
  switch (status)
  {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

String ESP_WiFiManager::getConfigPortalSSID()
{
  return _apName;
}

String ESP_WiFiManager::getConfigPortalPW()
{
  return _apPassword;
}

void ESP_WiFiManager::resetSettings()
{
  DEBUG_WM(F("previous settings invalidated"));
  WiFi.disconnect(true);
  delay(200);
  return;
}

void ESP_WiFiManager::setTimeout(unsigned long seconds)
{
  setConfigPortalTimeout(seconds);
}

void ESP_WiFiManager::setConfigPortalTimeout(unsigned long seconds)
{
  _configPortalTimeout = seconds * 1000;
}

void ESP_WiFiManager::setConnectTimeout(unsigned long seconds)
{
  _connectTimeout = seconds * 1000;
}

void ESP_WiFiManager::setDebugOutput(boolean debug)
{
  _debug = debug;
}

void ESP_WiFiManager::setAPStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn)
{
  _ap_static_ip = ip;
  _ap_static_gw = gw;
  _ap_static_sn = sn;
}

void ESP_WiFiManager::setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn)
{
  _sta_static_ip = ip;
  _sta_static_gw = gw;
  _sta_static_sn = sn;
}

#if USE_CONFIGURABLE_DNS
void ESP_WiFiManager::setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn, IPAddress dns_address_1, IPAddress dns_address_2)
{
  _sta_static_ip = ip;
  _sta_static_gw = gw;
  _sta_static_sn = sn;
  _sta_static_dns1 = dns_address_1; //***** Added argument *****
  _sta_static_dns2 = dns_address_2; //***** Added argument *****
}
#endif

void ESP_WiFiManager::setMinimumSignalQuality(int quality)
{
  _minimumQuality = quality;
}

void ESP_WiFiManager::setBreakAfterConfig(boolean shouldBreak)
{
  _shouldBreakAfterConfig = shouldBreak;
}

void ESP_WiFiManager::reportStatus(String &page)
{
  page += FPSTR(HTTP_SCRIPT_NTP_MSG);

  if (WiFi_SSID() != "")
  {
    page += F("配置为连接到接入点 <b>");
    page += WiFi_SSID();

    if (WiFi.status() == WL_CONNECTED)
    {
      page += F(" and currently connected</b> on IP <a href=\"http://");
      page += WiFi.localIP().toString();
      page += F("/\">");
      page += WiFi.localIP().toString();
      page += F("</a>");
    }
    else
    {
      page += F("</b>但是没有连接到该网络。");
    }
  }
  else
  {
    page += F("No network currently configured.");
  }
}

/** Handle root or redirect to captive portal */
void ESP_WiFiManager::handleRoot()
{
  DEBUG_WM(F("Handle root"));

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;		//KH

  if (captivePortal())
  {
    // If caprive portal redirect instead of displaying the error page.
    return;
  }

  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");

  String page = FPSTR(HTTP_HEAD_START);
  page.replace("{v}", "Options");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_SCRIPT_NTP);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += "<p><center><h2>";
  page += " qdprobot一键配网 ";
  page += "</h2></center></p>";
  page += FPSTR(HTTP_PORTAL_OPTIONS);
  page += F("<div class=\"msg\">");
  reportStatus(page);
  page += F("</div>");
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

}

/** Wifi config page handler */
void ESP_WiFiManager::handleWifi()
{
  DEBUG_WM(F("Handle WiFi"));

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;		//KH

  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  String page = FPSTR(HTTP_HEAD_START);
  page.replace("{v}", "Config ESP");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_SCRIPT_NTP);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("<h2>附近的WiFi</h2>");

  //  KH, New, v1.0.6+
  numberOfNetworks = scanWifiNetworks(&networkIndices);

  //Print list of WiFi networks that were found in earlier scan
  if (numberOfNetworks == 0)
  {
    page += F("WiFi scan found no networks. Restart configuration portal to scan again.");
  }
  else
  {
    //display networks in page
    for (int i = 0; i < numberOfNetworks; i++)
    {
      if (networkIndices[i] == -1)
        continue; // skip dups and those that are below the required quality

      DEBUG_WM(WiFi.SSID(networkIndices[i]));
      DEBUG_WM(WiFi.RSSI(networkIndices[i]));

      int quality = getRSSIasQuality(WiFi.RSSI(networkIndices[i]));

      String item = FPSTR(HTTP_ITEM);
      String rssiQ;
      rssiQ += quality;
      item.replace("{v}", WiFi.SSID(networkIndices[i]));
      item.replace("{r}", rssiQ);

#ifdef ESP8266
      if (WiFi.encryptionType(networkIndices[i]) != ENC_TYPE_NONE)
#else		//ESP32
      if (WiFi.encryptionType(networkIndices[i]) != WIFI_AUTH_OPEN)
#endif
      {
        item.replace("{i}", "l");
      }
      else
      {
        item.replace("{i}", "");
      }

      //DEBUG_WM(item);
      page += item;
      delay(0);
    }

    page += "<br/>";
  }

  page += FPSTR(HTTP_FORM_START);
  char parLength[2];

  // add the extra parameters to the form
  for (int i = 0; i < _paramsCount; i++)
  {
    if (_params[i] == NULL)
    {
      break;
    }

    String pitem;
    switch (_params[i]->getLabelPlacement())
    {
      case WFM_LABEL_BEFORE:
        pitem = FPSTR(HTTP_FORM_LABEL);
        pitem += FPSTR(HTTP_FORM_PARAM);
        break;
      case WFM_LABEL_AFTER:
        pitem = FPSTR(HTTP_FORM_PARAM);
        pitem += FPSTR(HTTP_FORM_LABEL);
        break;
      default:
        // WFM_NO_LABEL
        pitem = FPSTR(HTTP_FORM_PARAM);
        break;
    }

    if (_params[i]->getID() != NULL)
    {
      pitem.replace("{i}", _params[i]->getID());
      pitem.replace("{n}", _params[i]->getID());
      pitem.replace("{p}", _params[i]->getPlaceholder());
      snprintf(parLength, 2, "%d", _params[i]->getValueLength());
      pitem.replace("{l}", parLength);
      pitem.replace("{v}", _params[i]->getValue());
      pitem.replace("{c}", _params[i]->getCustomHTML());
    }
    else
    {
      pitem = _params[i]->getCustomHTML();
    }

    page += pitem;
  }

  if (_params[0] != NULL)
  {
    page += "<br/>";
  }

  if (_sta_static_ip)
  {
    String item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "ip");
    item.replace("{n}", "ip");
    item.replace("{p}", "Static IP");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_ip.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "gw");
    item.replace("{n}", "gw");
    item.replace("{p}", "Static Gateway");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_gw.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "sn");
    item.replace("{n}", "sn");
    item.replace("{p}", "Subnet");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_sn.toString());

#if USE_CONFIGURABLE_DNS
    //***** Added for DNS address options *****
    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "dns1");
    item.replace("{n}", "dns1");
    item.replace("{p}", "DNS Address 1");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_dns1.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "dns2");
    item.replace("{n}", "dns2");
    item.replace("{p}", "DNS Address 2");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_dns2.toString());
    //***** End added for DNS address options *****
#endif

    page += item;

    page += "<br/>";
  }

  page += FPSTR(HTTP_FORM_END);

  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent config page"));
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void ESP_WiFiManager::handleWifiSave()
{
  DEBUG_WM(F("WiFi save"));

  //SAVE/connect here
  _ssid = server->arg("s").c_str();
  _pass = server->arg("p").c_str();

  //parameters
  for (int i = 0; i < _paramsCount; i++)
  {
    if (_params[i] == NULL)
    {
      break;
    }

    //read parameter
    String value = server->arg(_params[i]->getID()).c_str();
    //store it in array
    value.toCharArray(_params[i]->_value, _params[i]->_length);
    DEBUG_WM(F("Parameter"));
    DEBUG_WM(_params[i]->getID());
    DEBUG_WM(value);
  }

  if (server->arg("ip") != "")
  {
    DEBUG_WM(F("static ip"));
    DEBUG_WM(server->arg("ip"));
    //_sta_static_ip.fromString(server->arg("ip"));
    String ip = server->arg("ip");
    optionalIPFromString(&_sta_static_ip, ip.c_str());
  }

  if (server->arg("gw") != "")
  {
    DEBUG_WM(F("static gateway"));
    DEBUG_WM(server->arg("gw"));
    String gw = server->arg("gw");
    optionalIPFromString(&_sta_static_gw, gw.c_str());
  }

  if (server->arg("sn") != "")
  {
    DEBUG_WM(F("static netmask"));
    DEBUG_WM(server->arg("sn"));
    String sn = server->arg("sn");
    optionalIPFromString(&_sta_static_sn, sn.c_str());
  }

#if USE_CONFIGURABLE_DNS
  //*****  Added for DNS Options *****
  if (server->arg("dns1") != "")
  {
    DEBUG_WM(F("DNS address 1"));
    DEBUG_WM(server->arg("dns1"));
    String dns1 = server->arg("dns1");
    optionalIPFromString(&_sta_static_dns1, dns1.c_str());
  }

  if (server->arg("dns2") != "")
  {
    DEBUG_WM(F("DNS address 2"));
    DEBUG_WM(server->arg("dns2"));
    String dns2 = server->arg("dns2");
    optionalIPFromString(&_sta_static_dns2, dns2.c_str());
  }
  //*****  End added for DNS Options *****
#endif

  String page = FPSTR(HTTP_HEAD_START);
  page.replace("{v}", "Credentials Saved");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_SCRIPT_NTP);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += FPSTR(HTTP_SAVED);
  page.replace("{v}", _apName);
  page.replace("{x}", _ssid);
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent wifi save page"));

  connect = true; //signal ready to connect/reset

  // Restore when Press Save WiFi
  _configPortalTimeout = DEFAULT_PORTAL_TIMEOUT;
}

/** Handle shut down the server page */
void ESP_WiFiManager::handleServerClose()
{
  DEBUG_WM(F("Server Close"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  String page = FPSTR(HTTP_HEAD_START);
  page.replace("{v}", "Close Server");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_SCRIPT_NTP);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("<div class=\"msg\">");
  page += F("当前网络为<b>");
  page += WiFi_SSID();
  page += F("</b><br>");
  page += F("当前连接IP地址<b>");
  page += WiFi.localIP().toString();
  page += F("</b><br><br>");
  page += F("关闭配置服务器...<br><br>");
  //page += F("Push button on device to restart configuration server!");
  page += FPSTR(HTTP_END);
  server->send(200, "text/html", page);
  //stopConfigPortal = true; //signal ready to shutdown config portal		//KH crash if use this ???
  DEBUG_WM(F("Sent server close page"));

  // Restore when Press Save WiFi
  _configPortalTimeout = DEFAULT_PORTAL_TIMEOUT;
}

/** Handle the info page */
void ESP_WiFiManager::handleInfo()
{
  DEBUG_WM(F("Info"));

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;		//KH

  //DEBUG_WM(F("Info"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  String page = FPSTR(HTTP_HEAD_START);
  page.replace("{v}", "Info");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_SCRIPT_NTP);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("<h2>WiFi信息</h2>");
  reportStatus(page);
  page += F("<h3>设备数据</h3>");
  page += F("<table class=\"table\">");
  page += F("<thead><tr><th>Name</th><th>Value</th></tr></thead><tbody><tr><td>芯片ID</td><td>");

#ifdef ESP8266
  page += String(ESP.getChipId(), HEX);		//ESP.getChipId();
#else		//ESP32
  page += String((uint32_t)ESP.getEfuseMac(), HEX);		//ESP.getChipId();
#endif

  page += F("</td></tr>");
  page += F("<tr><td>闪存芯片ID</td><td>");

#ifdef ESP8266
  page += String(ESP.getFlashChipId(), HEX);		//ESP.getFlashChipId();
#else		//ESP32
  // TODO
  page += F("TODO");
#endif

  page += F("</td></tr>");
  page += F("<tr><td>IDE闪存大小</td><td>");
  page += ESP.getFlashChipSize();
  page += F(" bytes</td></tr>");
  page += F("<tr><td>实际Flash大小</td><td>");

#ifdef ESP8266
  page += ESP.getFlashChipRealSize();
#else		//ESP32
  // TODO
  page += F("TODO");
#endif

  page += F(" bytes</td></tr>");
  page += F("<tr><td>接入点IP</td><td>");
  page += WiFi.softAPIP().toString();
  page += F("</td></tr>");
  page += F("<tr><td>接入点MAC</td><td>");
  page += WiFi.softAPmacAddress();
  page += F("</td></tr>");

  page += F("<tr><td>WIFI名称</td><td>");
  page += WiFi_SSID();
  page += F("</td></tr>");

  page += F("<tr><td>站点IP</td><td>");
  page += WiFi.localIP().toString();
  page += F("</td></tr>");

  page += F("<tr><td>站点MAC</td><td>");
  page += WiFi.macAddress();
  page += F("</td></tr>");
  page += F("</tbody></table>");

  page += FPSTR(HTTP_AVAILABLE_PAGES);

  page += F("<p/>");
  page += F("<p/>");
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent info page"));
}

/** Handle the state page */
void ESP_WiFiManager::handleState()
{
  DEBUG_WM(F("State - json"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  String page = F("{\"Soft_AP_IP\":\"");
  page += WiFi.softAPIP().toString();
  page += F("\",\"Soft_AP_MAC\":\"");
  page += WiFi.softAPmacAddress();
  page += F("\",\"Station_IP\":\"");
  page += WiFi.localIP().toString();
  page += F("\",\"Station_MAC\":\"");
  page += WiFi.macAddress();
  page += F("\",");

  if (WiFi.psk() != "")
  {
    page += F("\"Password\":true,");
  }
  else
  {
    page += F("\"Password\":false,");
  }

  page += F("\"SSID\":\"");
  page += WiFi_SSID();
  page += F("\"}");
  server->send(200, "application/json", page);
  DEBUG_WM(F("Sent state page in json format"));
}

/** Handle the scan page */
void ESP_WiFiManager::handleScan()
{
  DEBUG_WM(F("Scan"));

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;		//KH

  DEBUG_WM(F("State - json"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");

  int n;
  int *indices;

#if DEBUG_WIFIMGR
  Serial.printf("Indices = %08X, indicesptr = %08X\n", indices, &indices);
#endif

  //Space for indices array allocated on heap in scanWifiNetworks
  //and should be freed when indices no longer required.

  n = scanWifiNetworks(&indices);
  DEBUG_WM(F("In handleScan, scanWifiNetworks done"));
  String page = F("{\"Access_Points\":[");

  //display networks in page
  for (int i = 0; i < n; i++)
  {
    if (indices[i] == -1)
      continue; // skip duplicates and those that are below the required quality

    if (i != 0)
      page += F(", ");

    DEBUG_WM(WiFi.SSID(indices[i]));
    DEBUG_WM(WiFi.RSSI(indices[i]));

    int quality = getRSSIasQuality(WiFi.RSSI(indices[i]));
    String item = FPSTR(JSON_ITEM);
    String rssiQ;
    rssiQ += quality;
    item.replace("{v}", WiFi.SSID(indices[i]));
    item.replace("{r}", rssiQ);

#ifdef ESP8266
    if (WiFi.encryptionType(indices[i]) != ENC_TYPE_NONE)
#else		//ESP32
    if (WiFi.encryptionType(indices[i]) != WIFI_AUTH_OPEN)
#endif
    {
      item.replace("{i}", "true");
    }
    else
    {
      item.replace("{i}", "false");
    }
    //DEBUG_WM(item);
    page += item;
    delay(0);
  }

#if DEBUG_WIFIMGR
  Serial.printf("ESP_WiFiManager::handleScan : Indices = %08X, indicesptr = %08X\n", indices, &indices);
#endif

  if (indices)
  {
    free(indices); //indices array no longer required so free memory
  }

  page += F("]}");
  server->send(200, "application/json", page);
  DEBUG_WM(F("Sent WiFi scan data ordered by signal strength in json format"));
}

/** Handle the reset page */
void ESP_WiFiManager::handleReset()
{
  DEBUG_WM(F("Reset"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  String page = FPSTR(HTTP_HEAD_START);
  page.replace("{v}", "WiFi Information");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_SCRIPT_NTP);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("Module will reset in a few seconds.");
  page += FPSTR(HTTP_END);
  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent reset page"));
  delay(5000);
  WiFi.disconnect(true); // Wipe out WiFi credentials.

#ifdef ESP8266
  ESP.reset();
#else		//ESP32
  ESP.restart();
#endif

  delay(2000);
}

void ESP_WiFiManager::handleNotFound()
{
  if (captivePortal())
  {
    // If caprive portal redirect instead of displaying the error page.
    return;
  }

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += (server->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";

  for (uint8_t i = 0; i < server->args(); i++)
  {
    message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
  }

  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send(404, "text/plain", message);
}

/**
   HTTPD redirector
   Redirect to captive portal if we got a request for another domain.
   Return true in that case so the page handler do not try to handle the request again.
*/
boolean ESP_WiFiManager::captivePortal()
{
  if (!isIp(server->hostHeader()))
  {
    DEBUG_WM(F("Request redirected to captive portal"));
    server->sendHeader(F("Location"), (String)F("http://") + toStringIp(server->client().localIP()), true);
    server->send(302, FPSTR(HTTP_HEAD_CT2), ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server->client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

//start up config portal callback
void ESP_WiFiManager::setAPCallback(void(*func)(ESP_WiFiManager* myWiFiManager))
{
  _apcallback = func;
}

//start up save config callback
void ESP_WiFiManager::setSaveConfigCallback(void(*func)(void))
{
  _savecallback = func;
}

//sets a custom element to add to head, like a new style tag
void ESP_WiFiManager::setCustomHeadElement(const char* element) {
  _customHeadElement = element;
}

//if this is true, remove duplicated Access Points - defaut true
void ESP_WiFiManager::setRemoveDuplicateAPs(boolean removeDuplicates)
{
  _removeDuplicateAPs = removeDuplicates;
}

//Scan for WiFiNetworks in range and sort by signal strength
//space for indices array allocated on the heap and should be freed when no longer required
int ESP_WiFiManager::scanWifiNetworks(int **indicesptr)
{
#if DEBUG_WIFIMGR
  DEBUG_WM(F("Scanning Network"));
#endif

  int n = WiFi.scanNetworks();

#if DEBUG_WIFIMGR
  Serial.printf("ESP_WiFiManager::scanWifiNetworks : Scan done, no scanned Networks = %d\n", n);
#endif

  //KH, Terrible bug here. WiFi.scanNetworks() returns n < 0 => malloc( negative == very big ) => crash!!!
  //In .../esp32/libraries/WiFi/src/WiFiType.h
  //#define WIFI_SCAN_RUNNING   (-1)
  //#define WIFI_SCAN_FAILED    (-2)
  //if (n == 0)
  if (n <= 0)
  {
    DEBUG_WM(F("No networks found"));
    return (0);
  }
  else
  {
    // Allocate space off the heap for indices array.
    // This space should be freed when no longer required.
    int* indices = (int *)malloc(n * sizeof(int));

    if (indices == NULL)
    {
      DEBUG_WM(F("ERROR: Out of memory"));
      *indicesptr = NULL;
      return (0);
    }

    *indicesptr = indices;

#if DEBUG_WIFIMGR
    Serial.printf("ESP_WiFiManager::scanWifiNetworks : n = %d, Indices = %08X, indicesptr = %08X\n", n, indices, indicesptr);
#endif

    //sort networks
    for (int i = 0; i < n; i++)
    {
      indices[i] = i;
    }

#if DEBUG_WIFIMGR
    DEBUG_WM(F("Sorting"));
#endif

    // RSSI SORT
    // old sort
    for (int i = 0; i < n; i++)
    {
      for (int j = i + 1; j < n; j++)
      {
        if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i]))
        {
          std::swap(indices[i], indices[j]);
        }
      }
    }

#if DEBUG_WIFIMGR
    DEBUG_WM(F("Removing Dup"));
#endif

    // remove duplicates ( must be RSSI sorted )
    if (_removeDuplicateAPs)
    {
      String cssid;
      for (int i = 0; i < n; i++)
      {
        if (indices[i] == -1)
          continue;

        cssid = WiFi.SSID(indices[i]);
        for (int j = i + 1; j < n; j++)
        {
          if (cssid == WiFi.SSID(indices[j]))
          {
            DEBUG_WM("DUP AP: " + WiFi.SSID(indices[j]));
            indices[j] = -1; // set dup aps to index -1
          }
        }
      }
    }

    for (int i = 0; i < n; i++)
    {
      if (indices[i] == -1)
        continue; // skip dups

      int quality = getRSSIasQuality(WiFi.RSSI(indices[i]));

      if (!(_minimumQuality == -1 || _minimumQuality < quality))
      {
        indices[i] = -1;
        DEBUG_WM(F("Skipping due to quality"));
      }
    }

#if DEBUG_WIFIMGR
    for (int i = 0; i < n; i++)
    {
      if (indices[i] == -1)
        continue; // skip dups
      else
        Serial.println(WiFi.SSID(indices[i]));
    }
#endif

    return (n);
  }
}

template <typename Generic>
void ESP_WiFiManager::DEBUG_WM(Generic text)
{
  if (_debug)
  {
    Serial.print("*WM: ");
    Serial.println(text);
  }
}

int ESP_WiFiManager::getRSSIasQuality(int RSSI)
{
  int quality = 0;

  if (RSSI <= -100)
  {
    quality = 0;
  }
  else if (RSSI >= -50)
  {
    quality = 100;
  }
  else
  {
    quality = 2 * (RSSI + 100);
  }

  return quality;
}

/** Is this an IP? */
boolean ESP_WiFiManager::isIp(String str)
{
  for (int i = 0; i < str.length(); i++)
  {
    int c = str.charAt(i);

    if (c != '.' && (c < '0' || c > '9'))
    {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String ESP_WiFiManager::toStringIp(IPAddress ip)
{
  String res = "";
  for (int i = 0; i < 3; i++)
  {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }

  res += String(((ip >> 8 * 3)) & 0xFF);

  return res;
}

#ifdef ESP32
// We can't use WiFi.SSID() in ESP32 as it's only valid after connected.
// SSID and Password stored in ESP32 wifi_ap_record_t and wifi_config_t are also cleared in reboot
// Have to create a new function to store in EEPROM/SPIFFS for this purpose

String ESP_WiFiManager::getStoredWiFiSSID()
{
  if (WiFi.getMode() == WIFI_MODE_NULL)
  {
    return String();
  }

  wifi_ap_record_t info;

  if (!esp_wifi_sta_get_ap_info(&info))
  {
    return String(reinterpret_cast<char*>(info.ssid));
  }
  else
  {
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    return String(reinterpret_cast<char*>(conf.sta.ssid));
  }

  return String();
}

String ESP_WiFiManager::getStoredWiFiPass()
{
  if (WiFi.getMode() == WIFI_MODE_NULL)
  {
    return String();
  }

  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  return String(reinterpret_cast<char*>(conf.sta.password));
}
#endif

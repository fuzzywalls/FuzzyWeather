#include <ArduinoJson.h>
#include <PxMatrix.h>
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoHttpClient.h>
#include <NTPClient.h>
#include <string>

// ESP32 Pins for LED MATRIX
#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_OE 2

////////////////////////////////////////////////
// User definable variables. 
///////////////////////////////////////////////
char *WIFI_ACCESS_POINT = "";
char *WIFI_PASSWORD = "";
String API_KEY = "";  // OpenWeather API key
String LAT = "";
String LONG = "";

// Change this to whatever you want the splash message to be. 
String splashMessage = "FuzzyWeather: Powered by OpenWeather";

////////////////////////////////////////////////
// Globals
////////////////////////////////////////////////
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
DynamicJsonDocument weatherJson(15000);
unsigned long lastUpdate;
unsigned long elapsedtime; 
String weatherData;
int marquee_index = 0;
uint8_t display_draw_time=0; 
int isNight=0;

////////////////////////////////////////////////
// Initialize display
////////////////////////////////////////////////
PxMATRIX display(64,32,P_LAT, P_OE,P_A,P_B,P_C,P_D);
uint16_t blue = display.color565(0, 0, 255);
uint16_t yellow = display.color565(255, 255, 0);
uint16_t gray = display.color565(38, 38, 38);
uint16_t black = display.color565(0, 0, 0);
uint16_t white = display.color565(255, 255, 255);
uint16_t red = display.color565(255, 0, 0);
uint16_t green = display.color565(0, 255, 0);
uint16_t brown = display.color565(165, 42, 42);


void IRAM_ATTR display_updater(){
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL_ISR(&timerMux);
    display.display(display_draw_time);
    portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable){
    hw_timer_t *timer = NULL;
    if (is_enable) {
        timer = timerBegin(0, 80, true);
        timerAttachInterrupt(timer, &display_updater, true);
        timerAlarmWrite(timer, 2000, true);
        timerAlarmEnable(timer);
    }
    else {
        timerDetachInterrupt(timer);
        timerAlarmDisable(timer);
    }
}

/**********************************************************************
 *  setup()
 *  
 *  Initialize the display, setup serial debugging, get current time.
 **********************************************************************/
void setup() {
    Serial.begin(115200);
    
    connectWifi();

    display.begin(16);
    display.setFastUpdate(true);
    display_update_enable(true);
    display.setTextWrap(false);

    Serial.print("Updating time...\n");
    timeClient.begin();    
    timeClient.forceUpdate();
     
    getWeatherData();
    checkDayNight();
    lastUpdate = millis();

    splash_screen();
}

/**********************************************************************
 * loop()
 * 
 * Arduino loop. Connect to wifi is not connected, update weather 
 * every 10 minutes, check for day/night cycle.
 **********************************************************************/
void loop() { 
    if(WiFi.status() != WL_CONNECTED) {
        connectWifi();
    }
    
    elapsedtime = millis();
    if ((elapsedtime - lastUpdate) > 600000) {
        getWeatherData();
        checkDayNight();
        lastUpdate = millis();
    }
    timeClient.update();
    displayWeather();
}

/**********************************************************************
 * connectWifi()
 * 
 * Attempt to connect the ESP32 to wifi.
 **********************************************************************/
void connectWifi() {
    int attemptsRemaining = 5;
    do {
        Serial.print("Attempting to connect to Wifi\n");
        WiFi.begin(WIFI_ACCESS_POINT, WIFI_PASSWORD);
        delay(10000);
    }while (WiFi.status() != WL_CONNECTED && attemptsRemaining > 0);
}

/**********************************************************************
 * checkDayNight()
 * 
 * Check the current time versus sunset. Used to dim the display.
 **********************************************************************/
void checkDayNight() {
    unsigned long sunrise = weatherJson["daily"][0]["sunrise"].as<unsigned long>();
    unsigned long sunset = weatherJson["daily"][0]["sunset"].as<unsigned long>();
    unsigned long currTime = timeClient.getEpochTime();

    if (currTime < sunrise) {
        isNight=1;
        display.setBrightness(50);
    }
    else if (currTime < sunset) {
        isNight=0;
        display.setBrightness(255);
    }
    else {
        isNight=1;
        display.setBrightness(50);
    }
}

/**********************************************************************
 * getWeatherData()
 * 
 * Get weather data from api.openweathermap.org.
 **********************************************************************/
void getWeatherData(){                
    HTTPClient http;
    String endpoint = "https://api.openweathermap.org/data/2.5/onecall?units=imperial&exclude=hourly,minutely,alerts&lat=" + LAT + "&lon=" + LONG + "&appid=" + API_KEY;
    Serial.println(endpoint);
    http.begin(endpoint);
    
    int httpCode = http.GET();
    Serial.printf("HTTP response: %d\n", httpCode);
    
    if(httpCode > 0) {
        if(httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println(payload);
                                    
            DeserializationError error = deserializeJson(weatherJson, payload);
            if (error) {
                Serial.println("Error deserializing.");
            }
         }
        http.end();
    }
 }

/**********************************************************************
 * splash_weather()
 * 
 * Loop through all the weathers on the splash screen.
 **********************************************************************/
void splash_weather(int index) {
    if(index < -133) {
        drawFog();
    }
    else if(index < -103) {
        drawPrecip(25, gray);
    }
    else if (index < -79) {
        drawSun();
        drawClouds(100);
    }
    else if (index < -43) {
        drawMoon();
    }
    else if (index < -13) {
        drawSun();
    }
    else if (index < 17) {
        drawWind();
    }
    else if (index < 47) {
        drawClouds(10);
    }
    else {
        thunderStorm();
    }
}

/**********************************************************************
 * splash_screen()
 * 
 * Show this screen prior to displaying the weather. Just for fun splash
 * screen. 
 **********************************************************************/
 void splash_screen() {
    int msg_end = -6*splashMessage.length();
    for(int i = PxMATRIX_MAX_WIDTH; i > msg_end; i--){
        display.clearDisplay();
        splash_weather(i);
        display.setCursor(i,24);
        display.println(splashMessage);
        delay(100);    
    }
 }

/**********************************************************************
 * convertWindDirection()
 * 
 * Change meteroligical wind direction to compass direction.
 **********************************************************************/
String convertWindDirection(String windDirection) {
    float degree = windDirection.toFloat();
    
    if (degree < 5) {
        return "N";
    }
    else if (degree < 40) {
        return "NNE";
    }
    else if (degree < 50) {
        return "NE";
    }
    else if (degree < 85) {
        return "ENE";
    }
    else if (degree < 95) {
        return "E";
    }
    else if (degree < 130) {
        return "ESE";
    }
    else if (degree < 140) {
        return "SE";
    }
    else if (degree < 175) {
        return "SSE";
    }
    else if (degree < 185) {
        return "S";
    }
    else if (degree < 220) {
        return "SSW"; 
    }
    else if (degree < 230) {
        return "SW";
    }
    else if (degree < 265) {
        return "WSW";
    }
    else if (degree < 275) {
        return "W";
    }
    else if (degree < 310) {
        return "WNW";
    }
    else if(degree < 320) {
        return "NW";
    }
    else if (degree < 355) {
        return "NNW";
    }
    return "N";
}

/**********************************************************************
 * drawPrecip()
 * 
 * Draw precipitation on the screen based on current weather.
 **********************************************************************/
void drawPrecip(int amount, uint16_t color) {
    for (int n=0; n < amount; n++){
        display.drawPixel(random(0,32), random(0,24), color);
    }
}

/**********************************************************************
 * drawFog()
 * 
 * Draw fog with a tree.
 **********************************************************************/
void drawFog() {
    int fog_count = 500;
    static int fog_x[500];
    static int fog_y[500];
    static bool init = false;
    static int call_count = 0;

    // Draw a happy tree.
    display.fillRect(13, 20, 3, 4, brown);
    display.fillTriangle(9, 19, 14, 5, 22, 19, green);

    if (init == false || call_count == 50) {
        for (int i=0; i < fog_count; i++){
            fog_x[i] = random(0, 32);
            fog_y[i] = random(0, 24);
        }
        init = true;
        call_count = 0;
    }
    
    for (int i=0; i < fog_count; i++){
        display.drawPixel(fog_x[i], fog_y[i], gray);
    }

    call_count++;
}

void drawWind() {
    static int tree_top = 14;
    static int tree_sway = 0;
    static int tree_sway_max = 0;
    static bool sway_right = true;
    
    static int wind_height[6] = {0};
    static int wind_position[6] = {0};
    static int wind_length[6] = {0};
    static bool init = false;

    if(tree_sway == 0) {
        tree_sway_max = random(1, 6);
        sway_right = true;
    }

    if (tree_sway == tree_sway_max) {
        sway_right = false;
    }
    
    if(sway_right == true) {
        tree_sway++;
    }
    else {
        tree_sway--;
    }

    // Draw a happy tree.
    display.fillRect(14,19, 3, 4, brown);
    display.fillTriangle(9, 19, tree_top + tree_sway, 5, 22, 19, green);

    for (int i = 0; i < 6; i++) {
        if(wind_position[i] == 0 && random(0, 10) == 1) {
            wind_position[i] = 1;
            wind_length[i] = random(3, 8);
            wind_height[i] = random(0, 24);
        }

        if(wind_position[i] > 0) {
            display.drawLine(wind_position[i], wind_height[i], wind_position[i]+wind_length[i], wind_height[i], white);
            wind_position[i]++;
            if (wind_position[i] == 32) {
                wind_position[i] = 0;
            }
            if (wind_position[i] + wind_length[i] == 32) {
                wind_length[i]--;
            }
        }
        
    }  
}

/**********************************************************************
 * lightning()
 * 
 * Draw lighting. Very pleased with how this turned out. 
 **********************************************************************/
void lightning(int start_x, int start_y, int level) {
    if (level == 0) {
        return;
    }

    for(int i=0; i <= random(0, 3); i++) {
        int end_x = random(start_x-5, start_x + 5);
        int end_y = random(start_y, start_y + 5);
        display.drawLine(start_x, start_y, end_x, end_y, yellow);
        lightning(end_x, end_y, level-1);
    }
}

/**********************************************************************
 * thunderStorm()
 * 
 * Draw precipitation with random lightning flashes. 
 **********************************************************************/
void thunderStorm() {
    drawPrecip(100, blue);
    if(random(0, 10) == 1) {
        lightning(random(0, 25), 0, random(3, 6));
    }
}

/**********************************************************************
 * drawSun()
 * 
 * Draw a sun in the top left hand corner of the display.
 **********************************************************************/
void drawSun() {
    static bool flipRays = true;
    display.fillCircle(0, 0, 11, yellow);

    if (marquee_index % 7 == 0) {
        flipRays = !flipRays;
    }

    if (flipRays) {
        display.drawLine(11, 0, 19, 0, yellow);
        display.drawLine(11, 2, 14, 2, yellow);
        display.drawLine(11, 4, 14, 7, yellow);
        display.drawLine(8, 7, 14, 13, yellow);
        display.drawLine(5, 11, 8, 14, yellow);
        display.drawLine(0, 11, 0, 14, yellow);
        display.drawLine(3, 11, 3, 19, yellow);
    }
    else {
        display.drawLine(11, 0, 14, 0, yellow);
        display.drawLine(11, 2, 19, 2, yellow);
        display.drawLine(11, 4, 17, 10, yellow);
        display.drawLine(8, 7, 11, 10, yellow); 
        display.drawLine(5, 11, 11, 17, yellow);
        display.drawLine(0, 11, 0, 19, yellow);
        display.drawLine(3, 11, 3, 14, yellow);
    }
}

/**********************************************************************
 * drawMoon()
 * 
 * Draw a moon with stars the start dim and grow brighter, then move 
 * them to a new location.
 **********************************************************************/
void drawMoon() {
    static int star_x[25];
    static int star_y[25];
    static int star_intensity[25];
    static bool init = false;

    if (!init) {
        for (int i = 0; i < 25; i++) {
            star_x[i] = random(0, 32);
            star_y[i] = random(0, 24);
            star_intensity[i] = random(0, 255);
        }
        init = true;
    }

     for (int i = 0; i < 25; i++) {
         uint16_t intensity = display.color565(star_intensity[i], star_intensity[i], star_intensity[i]);
         display.writePixel(star_x[i], star_y[i], intensity);

         if(star_intensity[i] == 255) {
             star_x[i] = random(0, 32);
             star_y[i] = random(0, 24);
             star_intensity[i] = 0;
         }
         star_intensity[i]++;
     }

     display.fillCircle(15, 6, 5, white);
}

/**********************************************************************
 * drawClouds()
 * 
 * Draw clouds at random locations in the sky. 
 **********************************************************************/
void drawClouds(int percentage) {
    static bool init = false;
    static int call_count = 0;
    static int c1_x, c2_x, c3_x, c4_x, c5_x;
    static int c1_r, c2_r, c3_r, c4_r, c5_r;

    if (call_count == percentage || !init) {
        c1_x = random(0, 5);
        c2_x = random(5, 10);
        c3_x = random(10, 15);
        c4_x = random(15, 20);
        c5_x = random(20, 25);

        c1_r = random(0, 8);
        c2_r = random(0, 8);
        c3_r = random(0, 8);
        c4_r = random(0, 8);
        c5_r = random(0, 8);
        init = true;
        call_count = 0;
    }
    
    display.fillCircle(c1_x, 0, c1_r, gray);
    display.fillCircle(c2_x, 0, c2_r, gray);
    display.fillCircle(c3_x, 0, c3_r, gray);
    display.fillCircle(c4_x, 0, c4_r, gray);
    display.fillCircle(c5_x, 0, c5_r, gray);
    call_count++;
}

/**********************************************************************
 * drawWeather()
 * 
 * Draw weather based on values in the returned weather array. 
 **********************************************************************/
void drawWeather(int weatherValue) {
    // Thunderstorm
    if (250 > weatherValue && weatherValue >= 200) {
        thunderStorm();
    }
    // Drizzle
    else if (350 > weatherValue && weatherValue >= 300) {
        drawPrecip(20, blue);
    }
    else if (550 > weatherValue && weatherValue >= 500) {
        drawPrecip(100, blue);
    }
    else if (650 > weatherValue && weatherValue >= 600) {
        drawPrecip(25, gray);
    }
    else if (750 > weatherValue && weatherValue >= 700) {
        drawFog();
    }
    else if (weatherValue == 800 && !isNight) {
        drawSun();
    }
    else if (weatherValue == 800 && isNight) {
        drawMoon();
    }
    else if (810 > weatherValue && weatherValue > 800) {
        drawClouds();
    }
   // else if (weatherValue == "wind") {
   //     drawWind(); 
    //}
}

/**********************************************************************
 * displayWeather()
 * 
 * Display the weather most recently retrieved from openweathermap.org
 **********************************************************************/
 void displayWeather() {
    JsonArray& weather = weatherJson["daily"][0]["weather"];
    float tempFaren = weatherJson["current"]["temp"].as<float>();
    float feelsLike = weatherJson["current"]["feels_like"].as<float>();
    float highTemp = weatherJson["daily"][0]["temp"]["max"].as<float>();
    float lowTemp = weatherJson["daily"][0]["temp"]["min"].as<float>();
    float humidity = weatherJson["current"]["humidity"].as<float>();
    float windSpeed = weatherJson["daily"][0]["wind_speed"];
    String windDirection = convertWindDirection(weatherJson["daily"][0]["wind_deg"]);
    float cloudPercentage = weatherJson["current"]["clouds"].as<float>();
        
    String message = " Feels Like:" + String(feelsLike, 0) + "F Wind:" + windDirection + String(windSpeed,0) + "mph Cloud Cover:" + String(cloudPercentage, 0) + "%";
    int msg_end = -6*message.length();
    for(marquee_index = PxMATRIX_MAX_WIDTH; marquee_index > msg_end; marquee_index--){
        display.clearDisplay();
        for(int i=0; i<weather.size(); i++) {
          JsonObject& currWeather = weather_id[i];
          drawWeather(currWeather["id"]);
        }
        drawClouds((int)cloudPercentage);
        display.setCursor(46,0);
        display.printf("%2.00fF", tempFaren);
        display.setCursor(34, 8);
        display.printf("H:%2.00fF", highTemp);
        display.setCursor(34, 16);
        display.printf("L:%2.00fF", lowTemp);
        display.setCursor(marquee_index, 24);
        display.println(message);
        delay(100);    
    }
 }
 

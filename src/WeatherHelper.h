#ifndef WEATHER_HELPER_H
#define WEATHER_HELPER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "WeatherIcons.h"

struct WeatherData {
    float temp;
    int code;
    bool is_day;
    float wind_speed;
    bool valid;
};

class WeatherHelper {
public:
    static WeatherData getWSWWeather() {
        // Try Open-Meteo first
        WeatherData data = fetchOpenMeteo();
        if (data.valid) {
            Serial.println("Weather source: Open-Meteo");
            return data;
        }
        // Fallback to wttr.in
        data = fetchWttrIn();
        if (data.valid) {
            Serial.println("Weather source: wttr.in (fallback)");
            return data;
        }
        Serial.println("Weather: ALL sources failed!");
        return data;
    }

    static const uint8_t* getIconForCode(int code, bool is_day, float wind_speed) {
        // High Wind priority
        if (wind_speed > 30.0) {
            if (code >= 1 && code <= 3) return is_day ? cloud_wind_sun_bits : cloud_wind_moon_bits;
            return wind_bits;
        }

        switch (code) {
            case 0: return is_day ? sun_bits : moon_bits;
            case 1: case 2: return is_day ? cloud_sun_bits : cloud_moon_bits;
            case 3: return clouds_bits;
            case 45: case 48: return clouds_bits;
            case 51: case 53: case 55: return is_day ? rain0_sun_bits : rain0_bits;
            case 61: return is_day ? rain1_sun_bits : rain1_bits;
            case 63: case 65: return rain2_bits;
            case 71: case 73: case 75: return is_day ? snow_sun_bits : snow_moon_bits;
            case 77: return snou_bits;
            case 80: case 81: case 82: return is_day ? rain1_sun_bits : rain1_moon_bits;
            case 85: case 86: return is_day ? snow_sun_bits : snow_moon_bits;
            case 95: return lightning_bits;
            case 96: case 99: return rain_lightning_bits;
            default: return is_day ? sun_bits : moon_bits;
        }
    }

private:
    static WeatherData fetchOpenMeteo() {
        WeatherData data = {0, 0, true, 0, false};
        HTTPClient http;
        String url = "http://api.open-meteo.com/v1/forecast?latitude=52.2297&longitude=21.0122&current=temperature_2m,weather_code,is_day,wind_speed_10m&timezone=Europe%2FWarsaw";
        
        http.setTimeout(5000);
        http.begin(url);
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, payload)) {
                data.temp = doc["current"]["temperature_2m"];
                data.code = doc["current"]["weather_code"];
                data.is_day = doc["current"]["is_day"] == 1;
                data.wind_speed = doc["current"]["wind_speed_10m"];
                data.valid = true;
                Serial.printf("OpenMeteo OK: %.1f C, Code: %d\n", data.temp, data.code);
            }
        } else {
            Serial.printf("OpenMeteo FAIL: %d\n", httpCode);
        }
        http.end();
        return data;
    }

    // wttr.in returns WWO condition codes, map them to WMO codes
    static int wwoToWmo(int wwo) {
        if (wwo == 113) return 0;       // Clear/Sunny
        if (wwo == 116) return 2;       // Partly cloudy
        if (wwo == 119) return 3;       // Cloudy/Overcast
        if (wwo == 122) return 3;       // Overcast
        if (wwo == 143) return 45;      // Mist/Fog
        if (wwo == 176) return 80;      // Patchy rain
        if (wwo == 179) return 71;      // Patchy snow
        if (wwo == 182) return 77;      // Patchy sleet
        if (wwo == 185) return 51;      // Patchy freezing drizzle
        if (wwo == 200) return 95;      // Thunder
        if (wwo == 227) return 77;      // Blowing snow
        if (wwo == 230) return 75;      // Blizzard
        if (wwo == 248 || wwo == 260) return 48; // Fog
        if (wwo == 263 || wwo == 266) return 51; // Drizzle
        if (wwo == 281 || wwo == 284) return 55; // Freezing drizzle
        if (wwo == 293 || wwo == 296) return 61; // Light rain
        if (wwo == 299 || wwo == 302) return 63; // Moderate rain
        if (wwo == 305 || wwo == 308) return 65; // Heavy rain
        if (wwo == 311 || wwo == 314) return 55; // Freezing rain
        if (wwo == 317 || wwo == 320) return 77; // Sleet
        if (wwo == 323 || wwo == 326) return 71; // Light snow
        if (wwo == 329 || wwo == 332) return 73; // Moderate snow
        if (wwo == 335 || wwo == 338) return 75; // Heavy snow
        if (wwo == 350) return 77;      // Ice pellets
        if (wwo == 353 || wwo == 356) return 80; // Rain shower
        if (wwo == 359) return 82;      // Torrential rain
        if (wwo == 362 || wwo == 365) return 85; // Sleet shower
        if (wwo == 368 || wwo == 371) return 85; // Snow shower
        if (wwo == 374 || wwo == 377) return 77; // Ice pellets
        if (wwo == 386) return 95;      // Thunder
        if (wwo == 389) return 99;      // Thunder + heavy rain
        if (wwo == 392 || wwo == 395) return 96; // Thunder + snow
        return 0;
    }

    static WeatherData fetchWttrIn() {
        WeatherData data = {0, 0, true, 0, false};
        HTTPClient http;
        // wttr.in JSON format for Warsaw
        String url = "http://wttr.in/Warsaw?format=j1";
        
        http.setTimeout(8000);
        http.addHeader("User-Agent", "ESP32-Flipdot/1.0");
        http.begin(url);
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, payload)) {
                JsonObject cur = doc["current_condition"][0];
                data.temp = cur["temp_C"].as<float>();
                int wwoCode = cur["weatherCode"].as<int>();
                data.code = wwoToWmo(wwoCode);
                data.wind_speed = cur["windspeedKmph"].as<float>();
                
                // Determine day/night from astronomy data
                // Simple: check current hour vs sunrise/sunset
                String localTime = cur["localObsDateTime"].as<String>();
                int hour = localTime.substring(11,13).toInt();
                data.is_day = (hour >= 6 && hour < 20);
                
                data.valid = true;
                Serial.printf("wttr.in OK: %.1f C, WWO: %d -> WMO: %d\n", data.temp, wwoCode, data.code);
            }
        } else {
            Serial.printf("wttr.in FAIL: %d\n", httpCode);
        }
        http.end();
        return data;
    }
};

#endif

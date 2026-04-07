#include <Arduino.h>
#include <WiFi.h>
#include <FFat.h>

#include <FastBot2.h>
#include <RTClib.h>
#include "secrets.h"

FastBot2 bot;

// Data structure to hold timestamped sensor readings
struct SensorReading {
    DateTime timestamp;
    float value;
};

struct SensorHistory {
    const char* name;
    const char* unit;
    const char* color;
    int count = 10;
    SensorReading readings[10];
};

struct EventReading {
    DateTime timestamp;
    bool state;
};

struct EventHistory {
    const char* name;
    const char* emojiOn;
    const char* emojiOff;
    const char* onStr;
    const char* offStr;
    const char* color;
    int count = 10;
    EventReading readings[10];
};

struct LogEntry {
    DateTime timestamp;
    float grnhouseTemp;
    float grnhouseHum;
    float ambientTemp;
    float ambientHum;
    bool motorUp;
};

const char* SENSOR_LOG_PATH = "/sensor_data.txt";
const char* SENSOR_SVG_PATH = "/sensor_plot.svg";

SensorHistory grnhouseTempHistory{
    "Green House Temperature",
    "°F",
    "#ff4d4d",
    10,
    {
        {DateTime(2026, 4, 6, 8, 0), 74.2},
        {DateTime(2026, 4, 6, 8, 30), 75.0},
        {DateTime(2026, 4, 6, 9, 0), 76.4},
        {DateTime(2026, 4, 6, 9, 30), 78.1},
        {DateTime(2026, 4, 6, 10, 0), 80.0},
        {DateTime(2026, 4, 6, 10, 30), 81.8},
        {DateTime(2026, 4, 6, 11, 0), 83.2},
        {DateTime(2026, 4, 6, 11, 30), 84.5},
        {DateTime(2026, 4, 6, 12, 0), 85.1},
        {DateTime(2026, 4, 6, 12, 30), 84.0}
    }
};

SensorHistory grnhouseHumHistory{
    "Green House Humidity",
    "%",
    "#007bff",
    10,
    {
        {DateTime(2026, 4, 6, 8, 0), 68.0},
        {DateTime(2026, 4, 6, 8, 30), 67.2},
        {DateTime(2026, 4, 6, 9, 0), 66.4},
        {DateTime(2026, 4, 6, 9, 30), 65.0},
        {DateTime(2026, 4, 6, 10, 0), 63.8},
        {DateTime(2026, 4, 6, 10, 30), 62.1},
        {DateTime(2026, 4, 6, 11, 0), 60.7},
        {DateTime(2026, 4, 6, 11, 30), 59.5},
        {DateTime(2026, 4, 6, 12, 0), 58.8},
        {DateTime(2026, 4, 6, 12, 30), 60.2}
    }
};

SensorHistory ambientTempHistory{
    "Ambient Temperature",
    "°F",
    "#ffb54d",
    10,
    {
        {DateTime(2026, 4, 6, 8, 0), 58.4},
        {DateTime(2026, 4, 6, 8, 30), 59.2},
        {DateTime(2026, 4, 6, 9, 0), 60.1},
        {DateTime(2026, 4, 6, 9, 30), 61.5},
        {DateTime(2026, 4, 6, 10, 0), 62.8},
        {DateTime(2026, 4, 6, 10, 30), 64.0},
        {DateTime(2026, 4, 6, 11, 0), 65.2},
        {DateTime(2026, 4, 6, 11, 30), 66.1},
        {DateTime(2026, 4, 6, 12, 0), 67.0},
        {DateTime(2026, 4, 6, 12, 30), 67.4}
    }
};

SensorHistory ambientHumHistory{
    "Ambient Humidity",
    "%",
    "#9d00ff",
    10,
    {
        {DateTime(2026, 4, 6, 8, 0), 82.0},
        {DateTime(2026, 4, 6, 8, 30), 81.2},
        {DateTime(2026, 4, 6, 9, 0), 80.8},
        {DateTime(2026, 4, 6, 9, 30), 79.4},
        {DateTime(2026, 4, 6, 10, 0), 78.0},
        {DateTime(2026, 4, 6, 10, 30), 76.8},
        {DateTime(2026, 4, 6, 11, 0), 75.4},
        {DateTime(2026, 4, 6, 11, 30), 74.6},
        {DateTime(2026, 4, 6, 12, 0), 73.8},
        {DateTime(2026, 4, 6, 12, 30), 73.0}
    }
};

EventHistory sidesHistory{
    "Roller Sides",
    "🟧",
    "  ",
    "UP",
    "DOWN",
    "#ffa500",
    10,
    {
        {DateTime(2026, 4, 6, 8, 0), false},
        {DateTime(2026, 4, 6, 8, 30), false},
        {DateTime(2026, 4, 6, 9, 0), false},
        {DateTime(2026, 4, 6, 9, 30), true},
        {DateTime(2026, 4, 6, 10, 0), true},
        {DateTime(2026, 4, 6, 10, 30), true},
        {DateTime(2026, 4, 6, 11, 0), true},
        {DateTime(2026, 4, 6, 11, 30), true},
        {DateTime(2026, 4, 6, 12, 0), false},
        {DateTime(2026, 4, 6, 12, 30), false}
    }
};

// Globals for live dashboard test
fb::ID dashboardChatID = 0LL;
uint32_t dashboardMsgID = 0;
unsigned long dashboardTimer = 0;
bool dashboardActive = false;

// --- Interactive Configuration State & Variables ---
float targetTemp = 80.0;
float targetMoisture = 50.0;

struct SettingsParameter {
    String icon;
    String name;
    float* valueRef;
    String unit;
};

SettingsParameter settings[] = {
    {"🌡️", "Target Temp", &targetTemp, "°F"},
    {"💧", "Target Moist", &targetMoisture, "%"}
};
const int NUM_BOT_PARAMS = sizeof(settings) / sizeof(settings[0]);

enum BotState {
    STATE_IDLE, 
    STATE_AWAIT_VALUE
};
BotState botState = STATE_IDLE;
int activeConfigIndex = -1;

void tickMockData(SensorHistory& history) {
    // Generate new pseudo-random reading based on the previous last reading
    DateTime oldTime = history.readings[history.count - 1].timestamp;
    float oldVal = history.readings[history.count - 1].value;
    
    // Shift everything left
    for (int i = 0; i < history.count - 1; i++) {
        history.readings[i] = history.readings[i + 1];
    }
    
    // Increment time by 1 hour
    history.readings[history.count - 1].timestamp = oldTime + TimeSpan(0, 1, 0, 0); 
    
    // Fluctuate temp by -1.5 to +1.5 degrees
    long rnd = random(-15, 16); 
    history.readings[history.count - 1].value = oldVal + (rnd / 10.0);
}

void tickMockEventData(EventHistory& history) {
    DateTime oldTime = history.readings[history.count - 1].timestamp;
    bool oldState = history.readings[history.count - 1].state;
    
    // Shift everything left
    for (int i = 0; i < history.count - 1; i++) {
        history.readings[i] = history.readings[i + 1];
    }
    
    // Increment time by 1 hour
    history.readings[history.count - 1].timestamp = oldTime + TimeSpan(0, 1, 0, 0);
    
    // 20% chance to toggle state
    if (random(0, 100) < 20) {
        history.readings[history.count - 1].state = !oldState;
    } else {
        history.readings[history.count - 1].state = oldState;
    }
}

// Helper function to format DateTime object to a String (hh:mm)
String formatTime(DateTime dt) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour(), dt.minute());
    return String(buf);
}

bool parseSensorLogLine(const String& rawLine, LogEntry& entry) {
    String line = rawLine;
    line.trim();
    if (!line.length()) return false;

    int year, month, day, hour, minute, second, motorUpInt;
    float grnhouseTemp, grnhouseHum, ambientTemp, ambientHum;

    int parsed = sscanf(
        line.c_str(),
        "%d-%d-%d,%d:%d:%d,%f,%f,%f,%f,%d",
        &year,
        &month,
        &day,
        &hour,
        &minute,
        &second,
        &grnhouseTemp,
        &grnhouseHum,
        &ambientTemp,
        &ambientHum,
        &motorUpInt);

    if (parsed != 11) return false;

    entry.timestamp = DateTime(year, month, day, hour, minute, second);
    entry.grnhouseTemp = grnhouseTemp;
    entry.grnhouseHum = grnhouseHum;
    entry.ambientTemp = ambientTemp;
    entry.ambientHum = ambientHum;
    entry.motorUp = motorUpInt != 0;
    return true;
}

float getLogSeriesValue(const LogEntry& entry, int seriesIndex) {
    switch (seriesIndex) {
        case 0: return entry.grnhouseTemp;
        case 1: return entry.ambientTemp;
        case 2: return entry.grnhouseHum;
        case 3: return entry.ambientHum;
        default: return 0.0;
    }
}

const char* getLogSeriesName(int seriesIndex) {
    switch (seriesIndex) {
        case 0: return "Green House Temperature";
        case 1: return "Ambient Temperature";
        case 2: return "Green House Humidity";
        case 3: return "Ambient Humidity";
        default: return "Series";
    }
}

const char* getLogSeriesColor(int seriesIndex) {
    switch (seriesIndex) {
        case 0: return "#ff4d4d";
        case 1: return "#ffb54d";
        case 2: return "#007bff";
        case 3: return "#9d00ff";
        default: return "#000000";
    }
}

bool sendSvgGraphFromLog(fb::ID chatID, const char* logPath = SENSOR_LOG_PATH, const char* svgPath = SENSOR_SVG_PATH) {
    File input = FFat.open(logPath, "r");
    if (!input) {
        bot.sendMessage(fb::Message("❌ Error: sensor log file not found.", chatID));
        return false;
    }

    int count = 0;
    float minVal = 0.0;
    float maxVal = 0.0;
    bool hasValue = false;

    while (input.available()) {
        LogEntry entry;
        if (!parseSensorLogLine(input.readStringUntil('\n'), entry)) continue;

        for (int seriesIndex = 0; seriesIndex < 4; seriesIndex++) {
            float value = getLogSeriesValue(entry, seriesIndex);
            if (!hasValue) {
                minVal = value;
                maxVal = value;
                hasValue = true;
            } else {
                if (value < minVal) minVal = value;
                if (value > maxVal) maxVal = value;
            }
        }
        count++;
    }
    input.close();

    if (!hasValue || count == 0) {
        bot.sendMessage(fb::Message("❌ Error: sensor log file is empty or invalid.", chatID));
        return false;
    }
    if (maxVal == minVal) {
        maxVal += 1.0;
        minVal -= 1.0;
    }

    int pointSpacing = 60;
    if (count > 48) pointSpacing = 28;
    if (count > 96) pointSpacing = 16;

    int numEvents = 1;
    int eventRowHeight = 15;
    int marginYBottomBase = 30;
    int marginYBottom = marginYBottomBase + (numEvents * eventRowHeight);
    int marginX = 100;
    int width = (count * pointSpacing) + marginX + 20;
    int height = 265 + (numEvents * eventRowHeight);
    int marginYTop = 50;
    int graphHeight = height - marginYTop - marginYBottom;
    int labelStep = count / 8;
    if (labelStep < 1) labelStep = 1;
    int valueLabelStep = (pointSpacing < 40) ? 2 : 1;
    int gridStart = int(floor(minVal / 5.0f) * 5.0f);
    int gridEnd = int(ceil(maxVal / 5.0f) * 5.0f);

    FFat.remove(svgPath);
    File svg = FFat.open(svgPath, FILE_WRITE);
    if (!svg) {
        bot.sendMessage(fb::Message("❌ Error: could not create SVG output file.", chatID));
        return false;
    }

    svg.print("<svg viewBox=\"0 0 ");
    svg.print(width);
    svg.print(" ");
    svg.print(height);
    svg.println("\" xmlns=\"http://www.w3.org/2000/svg\" style=\"background-color:#ffffff; font-family:sans-serif;\">");
    svg.println("  <style>");
    svg.println("    .axis { font-size: 10px; fill: #666; }");
    svg.println("    .grid { stroke: #e6e6e6; stroke-width: 1; }");
    svg.println("    .title { font-size: 14px; font-weight: bold; fill: #333; }");
    svg.println("    .legend { font-size: 10px; font-weight: bold; }");
    svg.println("    .val { font-size: 10px; font-weight: bold; }");
    svg.println("  </style>");
    svg.print("  <text x=\"");
    svg.print(width / 2);
    svg.println("\" y=\"20\" class=\"title\" text-anchor=\"middle\">Greenhouse Log History</text>");
    svg.print("  <line x1=\"");
    svg.print(marginX);
    svg.print("\" y1=\"");
    svg.print(marginYTop);
    svg.print("\" x2=\"");
    svg.print(marginX);
    svg.print("\" y2=\"");
    svg.print(height - marginYBottom);
    svg.println("\" stroke=\"#ccc\" stroke-width=\"1.5\" />");

    for (int gridValue = gridStart; gridValue <= gridEnd; gridValue += 5) {
        int y = height - marginYBottom - int(((gridValue - minVal) / (maxVal - minVal)) * graphHeight);
        if (y < marginYTop || y > (height - marginYBottom)) continue;

        svg.print("  <line x1=\"");
        svg.print(marginX);
        svg.print("\" y1=\"");
        svg.print(y);
        svg.print("\" x2=\"");
        svg.print(width - 20);
        svg.print("\" y2=\"");
        svg.print(y);
        svg.println("\" class=\"grid\" />");

        svg.print("  <text x=\"");
        svg.print(marginX - 5);
        svg.print("\" y=\"");
        svg.print(y + 4);
        svg.print("\" class=\"axis\" text-anchor=\"end\">");
        svg.print(gridValue);
        svg.println("</text>");
    }

    svg.print("  <line x1=\"");
    svg.print(marginX);
    svg.print("\" y1=\"");
    svg.print(height - marginYBottom);
    svg.print("\" x2=\"");
    svg.print(width - 20);
    svg.print("\" y2=\"");
    svg.print(height - marginYBottom);
    svg.println("\" stroke=\"#ccc\" stroke-width=\"1.5\" />");
    input = FFat.open(logPath, "r");
    int index = 0;
    while (input.available()) {
        LogEntry entry;
        if (!parseSensorLogLine(input.readStringUntil('\n'), entry)) continue;

        int x = marginX + (index * pointSpacing) + (pointSpacing / 2);
        if ((index % labelStep) == 0 || index == count - 1) {
            svg.print("  <text x=\"");
            svg.print(x);
            svg.print("\" y=\"");
            svg.print(height - marginYBottom + 16);
            svg.print("\" class=\"axis\" text-anchor=\"middle\">");
            svg.print(formatTime(entry.timestamp));
            svg.println("</text>");
        }

        if (entry.motorUp) {
            svg.print("  <rect x=\"");
            svg.print(marginX + (index * pointSpacing));
            svg.print("\" y=\"");
            svg.print(height - marginYBottom + marginYBottomBase - 8);
            svg.print("\" width=\"");
            svg.print(pointSpacing);
            svg.println("\" height=\"10\" fill=\"#ffa500\" opacity=\"0.6\" />");
        }

        index++;
    }
    input.close();

    svg.print("  <text x=\"");
    svg.print(marginX - 5);
    svg.print("\" y=\"");
    svg.print(height - marginYBottom + marginYBottomBase);
    svg.println("\" class=\"axis\" text-anchor=\"end\">Roller Sides</text>");

    for (int seriesIndex = 0; seriesIndex < 4; seriesIndex++) {
        svg.print("  <text x=\"");
        svg.print(marginX + 10 + (seriesIndex * 110));
        svg.print("\" y=\"40\" class=\"legend\" fill=\"");
        svg.print(getLogSeriesColor(seriesIndex));
        svg.print("\">");
        svg.print(getLogSeriesName(seriesIndex));
        svg.println("</text>");

        svg.print("  <polyline fill=\"none\" stroke=\"");
        svg.print(getLogSeriesColor(seriesIndex));
        svg.println("\" stroke-width=\"2\" points=\"");

        input = FFat.open(logPath, "r");
        index = 0;
        while (input.available()) {
            LogEntry entry;
            if (!parseSensorLogLine(input.readStringUntil('\n'), entry)) continue;

            int x = marginX + (index * pointSpacing) + (pointSpacing / 2);
            int y = height - marginYBottom - int(((getLogSeriesValue(entry, seriesIndex) - minVal) / (maxVal - minVal)) * graphHeight);
            svg.print("    ");
            svg.print(x);
            svg.print(",");
            svg.print(y);
            svg.println();
            index++;
        }
        input.close();
        svg.println("  \" />");

        input = FFat.open(logPath, "r");
        index = 0;
        while (input.available()) {
            LogEntry entry;
            if (!parseSensorLogLine(input.readStringUntil('\n'), entry)) continue;

            int x = marginX + (index * pointSpacing) + (pointSpacing / 2);
            float value = getLogSeriesValue(entry, seriesIndex);
            int y = height - marginYBottom - int(((value - minVal) / (maxVal - minVal)) * graphHeight);

            svg.print("  <circle cx=\"");
            svg.print(x);
            svg.print("\" cy=\"");
            svg.print(y);
            svg.print("\" r=\"3.5\" fill=\"");
            svg.print(getLogSeriesColor(seriesIndex));
            svg.println("\" />");

            if ((index % valueLabelStep) == 0 || index == count - 1) {
                int yOffset = (seriesIndex % 2 == 0) ? -8 : 14;
                svg.print("  <text x=\"");
                svg.print(x);
                svg.print("\" y=\"");
                svg.print(y + yOffset);
                svg.print("\" class=\"val\" fill=\"");
                svg.print(getLogSeriesColor(seriesIndex));
                svg.print("\" text-anchor=\"middle\">");
                svg.print(String(value, 1));
                svg.println("</text>");
            }

            index++;
        }
        input.close();
    }

    svg.println("</svg>");
    svg.close();

    File svgFile = FFat.open(svgPath, "r");
    if (!svgFile) {
        bot.sendMessage(fb::Message("❌ Error: could not reopen generated SVG file.", chatID));
        return false;
    }

    fb::File fileAttachment("sensor_plot.svg", fb::File::Type::document, svgFile);
    fileAttachment.caption = "Sensor Value Trend from sensor_data.txt";
    fileAttachment.chatID = chatID;
    bot.sendFile(fileAttachment, true);
    svgFile.close();
    return true;
}

// 1. Unicode Graph Function (Sparkline)
uint32_t sendUnicodeGraph(fb::ID chatID, SensorHistory* histories, int numHistories, EventHistory* events = nullptr, int numEvents = 0, uint32_t editMsgID = 0, int width = 32) {
    if (numHistories == 0 || histories[0].count == 0) return 0;
    int count = histories[0].count; // Assumes synchronized timestamps

    // Unicode block elements for drawing a vertical sparkline:  ▂▃▄▅▆▇
    const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    float blocksPerReading = (float)width / count;
    
    String title = "Greenhouse Sensor";
    if (editMsgID > 0) title += " Dashboard";
    else title += " Trend";
    
    String msgText = "<b>" + title + "</b>\n\n";

    // --- Render Binary Event Histories (Fans, Sides, etc) ---
    if (numEvents > 0) {
        msgText += "<u><b>Actuators: </b></u>\n";
        for (int e = 0; e < numEvents; e++) {
            String emjStr = "";
            int blocksAdded = 0;
            
            for (int i = 0; i < count; i++) {
                // Emojis are visually 2 characters wide in monospace layout, so we target half the total width blocks
                int targetTotalBlocks = int((i + 1) * (blocksPerReading / 2.0)) - 1; // -1 to prevent overshooting due to emoji width
                int blocksToAdd = targetTotalBlocks - blocksAdded;
                
                for (int j = 0; j < blocksToAdd; j++) {
                    if (events[e].readings[i].state) emjStr += events[e].emojiOn;
                    else emjStr += events[e].emojiOff;
                    blocksAdded++;
                }
            }
            
            bool currentState = events[e].readings[count - 1].state;
            String stateStr = currentState ? events[e].onStr : events[e].offStr;
            msgText += events[e].name;
            msgText += ": " + stateStr + "\n";
            // Emoji line might not line up perfectly with monospace depending on Telegram OS client, but `<code>` block isn't great for Emojis.
            msgText += "<code>" + emjStr + "</code>\n";
        }
    }

    // Formatting the timestamps
    String startTimeStr = formatTime(histories[0].readings[0].timestamp);
    String midTimeStr = formatTime(histories[0].readings[count/2].timestamp);
    String endTimeStr = formatTime(histories[0].readings[count-1].timestamp);
    
    int remainingSpaces = width - startTimeStr.length() - midTimeStr.length() - endTimeStr.length();
    if (remainingSpaces < 0) remainingSpaces = 0;
    
    int space1 = remainingSpaces / 2;
    int space2 = remainingSpaces - space1;
    
    String spaceBuf1 = "";
    for (int i = 0; i < space1; i++) spaceBuf1 += " ";
    String spaceBuf2 = "";
    for (int i = 0; i < space2; i++) spaceBuf2 += " ";
    
    String timeAxis = "<code>" + startTimeStr + spaceBuf1 + midTimeStr + spaceBuf2 + endTimeStr + "</code>\n";

    msgText += timeAxis;


    // --- Render Analog Sensor Histories ---
    msgText += "\n<u><b>Sensor Readings: </b></u>\n";
    for (int h = 0; h < numHistories; h++) {
        float min_val = histories[h].readings[0].value;
        float max_val = histories[h].readings[0].value;
        float sum_val = 0;
        
        for (int i = 0; i < count; i++) {
            if (histories[h].readings[i].value < min_val) min_val = histories[h].readings[i].value;
            if (histories[h].readings[i].value > max_val) max_val = histories[h].readings[i].value;
            sum_val += histories[h].readings[i].value;
        }
        float avg_val = sum_val / count;

        String sparkline = "";
        int blocksAdded = 0;
        
        for (int i = 0; i < count; i++) {
            int block_idx = 0;
            if (max_val > min_val) {
                // Scale reading to an index between 0 and 7
                block_idx = int(((histories[h].readings[i].value - min_val) / (max_val - min_val)) * 7);
            }
            
            // Calculate how many total blocks should exist by this point
            int targetTotalBlocks = int((i + 1) * blocksPerReading);
            int blocksToAdd = targetTotalBlocks - blocksAdded;
            
            for (int j = 0; j < blocksToAdd; j++) {
                sparkline += blocks[block_idx];
                blocksAdded++;
            }
        }
        
        String nameStr = String(histories[h].name);

        float current_val = histories[h].readings[count-1].value;
        String trendStr = "→";
        if (count > 1) {
            float prev_val = histories[h].readings[count-2].value;
            if (current_val > prev_val) trendStr = "↗";
            else if (current_val < prev_val) trendStr = "↘";
        }
        
        msgText += "<b>" + nameStr + ":</b> " + String(current_val, 1) + " " + String(histories[h].unit) + " " + trendStr + "\n";
        msgText += "<code>" + sparkline + "\n";
        msgText += "min: " + String(min_val, 1);
        msgText += " max: " + String(max_val, 1);
        msgText += " avg: " + String(avg_val, 1);
        msgText += "\n</code>";
    }
    msgText += timeAxis;

    if (editMsgID == 0) {
        fb::Message msg(msgText, chatID);
        msg.setModeHTML(); // Important: required to render the <b> and <pre> tags
        bot.sendMessage(msg);
        return bot.lastBotMessage();
    } else {
        fb::TextEdit msg(msgText, editMsgID, chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.editText(msg);
        return editMsgID;
    }
}

// 2. SVG Graph Function (Line Chart)
void sendSvgGraph(fb::ID chatID, SensorHistory* histories, int numHistories, EventHistory* events = nullptr, int numEvents = 0) {
    if (numHistories == 0 || histories[0].count == 0) return;
    int count = histories[0].count; // Assumes all histories have the same count and synchronized timestamps

    float min_val = histories[0].readings[0].value;
    float max_val = histories[0].readings[0].value;
    for (int h = 0; h < numHistories; h++) {
        for (int i = 0; i < count; i++) {
            if (histories[h].readings[i].value < min_val) min_val = histories[h].readings[i].value;
            if (histories[h].readings[i].value > max_val) max_val = histories[h].readings[i].value;
        }
    }
    
    if (max_val == min_val) { max_val += 1.0; min_val -= 1.0; } // Prevent division by zero

    int eventRowHeight = 15;
    int marginYBottomBase = 30; // Bottom margin for X axis labels
    int marginYBottom = marginYBottomBase + (numEvents * eventRowHeight); 

    // Configure canvas size based on number of points
    int pointSpacing = 60;
    int marginX = 80; // Left margin for Y axis labels and event names
    int width = (count * pointSpacing) + marginX + 20; // Extra room for axes and title
    int height = 250 + (numEvents * eventRowHeight);
    int marginYTop = 50; // Top margin for Title and Legend
    int graphWidth = width - marginX - 20;
    int graphHeight = height - marginYTop - marginYBottom;
    
    // Start SVG document with responsive viewBox
    String svg = "<svg viewBox=\"0 0 " + String(width) + " " + String(height) + "\" xmlns=\"http://www.w3.org/2000/svg\" style=\"background-color:#ffffff; font-family:sans-serif;\">\n";
    svg += "  <style>\n";
    svg += "    .axis { font-size: 10px; fill: #666; }\n";
    svg += "    .title { font-size: 14px; font-weight: bold; fill: #333; }\n";
    svg += "    .legend { font-size: 10px; font-weight: bold; }\n";
    svg += "    .val { font-size: 10px; font-weight: bold; }\n";
    svg += "  </style>\n";
    
    // Background and Title
    String title = (numHistories == 1) ? String(histories[0].name) : "Combined Sensor";
    svg += "  <text x=\"" + String(width / 2) + "\" y=\"20\" class=\"title\" text-anchor=\"middle\">" + title + " History</text>\n";

    // Draw Axes
    svg += "  <line x1=\"" + String(marginX) + "\" y1=\"" + String(marginYTop) + "\" x2=\"" + String(marginX) + "\" y2=\"" + String(height - marginYBottom) + "\" stroke=\"#ccc\" stroke-width=\"1.5\" />\n"; // Y Axis
    svg += "  <line x1=\"" + String(marginX) + "\" y1=\"" + String(height - marginYBottom) + "\" x2=\"" + String(width - 20) + "\" y2=\"" + String(height - marginYBottom) + "\" stroke=\"#ccc\" stroke-width=\"1.5\" />\n"; // X Axis

    // Y Axis Labels
    svg += "  <text x=\"" + String(marginX - 5) + "\" y=\"" + String(marginYTop + 4) + "\" class=\"axis\" text-anchor=\"end\">" + String(max_val, 1) + "</text>\n";
    svg += "  <text x=\"" + String(marginX - 5) + "\" y=\"" + String(height - marginYBottom + 4) + "\" class=\"axis\" text-anchor=\"end\">" + String(min_val, 1) + "</text>\n";
    
    // X-Axis Time Labels (Drawn once for the primary timescale)
    for (int i = 0; i < count; i++) {
        int x = marginX + (i * pointSpacing) + (pointSpacing / 2);
        svg += "  <text x=\"" + String(x) + "\" y=\"" + String(height - marginYBottom + 16) + "\" class=\"axis\" text-anchor=\"middle\">" + formatTime(histories[0].readings[i].timestamp) + "</text>\n";
    }

    // Draw Blocks for Events (ON State)
    if (numEvents > 0) {
        for (int e = 0; e < numEvents; e++) {
            String color = events[e].color ? String(events[e].color) : "#000000";
            int yEvent = height - marginYBottom + marginYBottomBase + (e * eventRowHeight) - 8;
            
            // Draw Event Title on Y Axis
            svg += "  <text x=\"" + String(marginX - 5) + "\" y=\"" + String(yEvent + 8) + "\" class=\"axis\" text-anchor=\"end\">" + String(events[e].name) + "</text>\n";
            
            // Draw Rectangles for Active States
            for (int i = 0; i < events[e].count; i++) {
                if (events[e].readings[i].state) {
                    int xRect = marginX + (i * pointSpacing);
                    svg += "  <rect x=\"" + String(xRect) + "\" y=\"" + String(yEvent) + "\" width=\"" + String(pointSpacing) + "\" height=\"10\" fill=\"" + color + "\" opacity=\"0.6\" />\n";
                }
            }
        }
    }

    // Start drawing data lines for each history
    for (int h = 0; h < numHistories; h++) {
        String color = histories[h].color ? String(histories[h].color) : "#000000";
        
        // Draw Legend for this line
        int legendX = marginX + 10 + (h * 80);
        svg += "  <text x=\"" + String(legendX) + "\" y=\"40\" class=\"legend\" fill=\"" + color + "\">" + String(histories[h].name) + "</text>\n";

        // Line
        svg += "  <polyline fill=\"none\" stroke=\"" + color + "\" stroke-width=\"2\" points=\"";
        for (int i = 0; i < count; i++) {
            int x = marginX + (i * pointSpacing) + (pointSpacing / 2);
            int y = height - marginYBottom - int(((histories[h].readings[i].value - min_val) / (max_val - min_val)) * graphHeight);
            svg += String(x) + "," + String(y) + " ";
        }
        svg += "\" />\n";
        
        // Draw points and values
        for (int i = 0; i < count; i++) {
            int x = marginX + (i * pointSpacing) + (pointSpacing / 2);
            int y = height - marginYBottom - int(((histories[h].readings[i].value - min_val) / (max_val - min_val)) * graphHeight);
            
            // Value point
            svg += "  <circle cx=\"" + String(x) + "\" cy=\"" + String(y) + "\" r=\"3.5\" fill=\"" + color + "\" />\n";
            
            // Offset the value string vertically to reduce overlap between lines (basic heuristic)
            int yOffset = (h % 2 == 0) ? -8 : 14; 
            svg += "  <text x=\"" + String(x) + "\" y=\"" + String(y + yOffset) + "\" class=\"val\" fill=\"" + color + "\" text-anchor=\"middle\">" + String(histories[h].readings[i].value, 1) + "</text>\n";
        }
    }
    svg += "</svg>";

    // Package the raw string into a virtual file 
    fb::File file("graph.svg", fb::File::Type::document, (const uint8_t*)svg.c_str(), svg.length());
    file.caption = "Sensor Value Trend";
    file.chatID = chatID;
    
    // Send the document attachment
    bot.sendFile(file);
}

// Reusable method for spawning or updating the Main config inline menu
void sendConfigMainMenu(fb::ID chatID, uint32_t msgID = 0) {
    String txt = "<b>⚙️ Interactive Configuration</b>\nSelect a parameter to adjust:";
    String lbls = "";
    String cbs = ""; 
    
    for (int i = 0; i < NUM_BOT_PARAMS; i++) {
        lbls += settings[i].icon + " " + settings[i].name + " (" + String(*(settings[i].valueRef), 1) + settings[i].unit + ")";
        cbs += "csel_" + String(i);
        
        if (i < NUM_BOT_PARAMS - 1) {
            lbls += "\n";
            cbs += ";";
        }
    }
    
    fb::InlineMenu menu(lbls, cbs);
    
    if (msgID == 0) {
        fb::Message msg(txt, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.sendMessage(msg);
    } else {
        fb::TextEdit msg(txt, msgID, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.editText(msg);
    }
}

// Reusable method for spawning or updating the parameter adjustment inline menu
void sendConfigEditMenu(fb::ID chatID, uint32_t msgID, int paramIndex) {
    if (paramIndex < 0 || paramIndex >= NUM_BOT_PARAMS) return;
    SettingsParameter& p = settings[paramIndex];

    String txt = "<b>" + p.icon + " Editing " + p.name + "</b>\nCurrent Value: <code>" + String(*(p.valueRef), 1) + p.unit + "</code>";
    
    String lbls = "+10.0;+1.0;+0.1\n"
                  "-10.0;-1.0;-0.1\n"
                  "🔙 Back";
                  
    String cbs = "cadd_3_" + String(paramIndex) + ";cadd_2_" + String(paramIndex) + ";cadd_1_" + String(paramIndex) + ";" +
                 "csub_3_" + String(paramIndex) + ";csub_2_" + String(paramIndex) + ";csub_1_" + String(paramIndex) + ";conf_back";
                 
    fb::InlineMenu menu(lbls, cbs);
    
    fb::TextEdit msg(txt, msgID, chatID);
    msg.mode = fb::Message::Mode::HTML;
    msg.setInlineMenu(menu);
    bot.editText(msg);
}

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    // RGB LEDs are active-low on Arduino Nano ESP32
    analogWrite(LEDR, 255 - r);
    analogWrite(LEDG, 255 - g);
    analogWrite(LEDB, 255 - b);
}

void updateh(fb::Update& u) {
    if (u.isQuery()) {
        Serial.println("NEW QUERY");
        Serial.println(u.query().data());

        fb::ID chatID = u.query().message().chat().id();
        uint32_t msgID = u.query().message().id();

        // React to query data
        String qData = u.query().data();

        if (qData.startsWith("csel_")) {
            int idx = qData.substring(5).toInt();
            activeConfigIndex = idx;
            sendConfigEditMenu(chatID, msgID, idx);
            bot.answerCallbackQuery(u.query().id());
            return;
        }

        if (qData.startsWith("cadd_") || qData.startsWith("csub_")) {
            bool isAdd = qData.startsWith("cadd_");
            int stepLevel = qData.substring(5, 6).toInt();
            int idx = qData.substring(7).toInt();

            if (idx >= 0 && idx < NUM_BOT_PARAMS) {
                float amt = 0;
                if (stepLevel == 3) amt = 10.0;
                else if (stepLevel == 2) amt = 1.0;
                else if (stepLevel == 1) amt = 0.1;
                
                if (isAdd) *(settings[idx].valueRef) += amt;
                else *(settings[idx].valueRef) -= amt;
                
                sendConfigEditMenu(chatID, msgID, idx);
            }
            bot.answerCallbackQuery(u.query().id());
            return;
        }

        switch (u.query().data().hash()) {
            // Interactive config routing
            case "conf_back"_h:
                sendConfigMainMenu(chatID, msgID);
                bot.answerCallbackQuery(u.query().id());
                break;
            
            // Color choices
            case "red"_h:
                setLedColor(255, 0, 0);
                bot.answerCallbackQuery(u.query().id(), "Color changed");
                break;
            case "green"_h:
                setLedColor(0, 255, 0);
                bot.answerCallbackQuery(u.query().id(), "Color changed");
                break;
            case "blue"_h:
                setLedColor(0, 0, 255);
                bot.answerCallbackQuery(u.query().id(), "Color changed");
                break;
            case "orange"_h:
                setLedColor(255, 128, 0);
                bot.answerCallbackQuery(u.query().id(), "Color changed");
                break;
            case "yellow"_h:
                setLedColor(255, 255, 0);
                bot.answerCallbackQuery(u.query().id(), "Color changed");
                break;
            case "purple"_h:
                setLedColor(128, 0, 128);
                bot.answerCallbackQuery(u.query().id(), "Color changed");
                break;
        }
    } else if (u.isMessage()) {
        Serial.println("NEW MESSAGE");
        Serial.println(u.message().text());
        
        String text = u.message().text();
        fb::ID chatID = u.message().chat().id();

        // Intercept message if we are waiting for a value update
        if (botState == STATE_AWAIT_VALUE && activeConfigIndex >= 0 && activeConfigIndex < NUM_BOT_PARAMS) {
            float newVal = text.toFloat(); // Returns 0.0 if invalid
            *(settings[activeConfigIndex].valueRef) = newVal;
            
            fb::Message msg("✅ " + settings[activeConfigIndex].name + " updated to: " + String(newVal, 1) + settings[activeConfigIndex].unit, chatID);
            bot.sendMessage(msg);
            
            botState = STATE_IDLE;
            return; // Stop processing further commands
        }
        
        if (text == "/start") {
            fb::Message msg("Welcome! Use the menu below or commands like /config, /color, /unicode, and /svg.", chatID);
            bot.sendMessage(msg);

        } else if (text == "/config" || text == "Settings") {
            sendConfigMainMenu(chatID);

        } else if (text == "/set") {
            if (activeConfigIndex >= 0 && activeConfigIndex < NUM_BOT_PARAMS) {
                botState = STATE_AWAIT_VALUE;
                bot.sendMessage(fb::Message("Please type the new " + settings[activeConfigIndex].name + " (e.g. 85.5):", chatID));
            } else {
                bot.sendMessage(fb::Message("Please use /config to select a parameter first.", chatID));
            }

        } else if (text == "/color" || text == "Set Color") {
            fb::Message msg("Choose a color for the RGB LED:", chatID);
            fb::InlineMenu menu("Red;Green;Blue\nOrange;Yellow;Purple", "red;green;blue;orange;yellow;purple");
            msg.setInlineMenu(menu);
            bot.sendMessage(msg);

        } else if (text == "/unicode" || text == "Unicode Graph") {
            SensorHistory histories[] = {grnhouseTempHistory, ambientTempHistory, grnhouseHumHistory, ambientHumHistory};
            EventHistory events[] = {sidesHistory};
            sendUnicodeGraph(u.message().chat().id(), histories, 4, events, 1);

        } else if (text == "/dashboard" || text == "Live Dashboard") {
            dashboardChatID = u.message().chat().id();
            bot.sendMessage(fb::Message("⏳ Initializing Live Dashboard...", dashboardChatID));
            dashboardMsgID = bot.lastBotMessage();
            dashboardActive = true;
            dashboardTimer = millis() - 5000; // Trigger almost immediately

        } else if (text == "/svg" || text == "SVG Graph") {
            sendSvgGraphFromLog(u.message().chat().id());
            
        } else if (text == "/download" || text == "Download Log") {
            File logFile = FFat.open(SENSOR_LOG_PATH, "r");
            if (!logFile) {
                fb::Message msg("❌ Error: Log file not found on device.", u.message().chat().id());
                bot.sendMessage(msg);
            } else {
                bot.sendMessage(fb::Message("⏳ Fetching log file...", u.message().chat().id()));
                
                fb::File fileAttachment("sensor_data.txt", fb::File::Type::document, logFile);
                fileAttachment.caption = "Latest historical log data.";
                fileAttachment.chatID = u.message().chat().id();
                
                bot.sendFile(fileAttachment);
                logFile.close();
            }
            
        } else {
            // Echo back with instructions
            fb::Message msg("Command not recognized. Please use the menu.", u.message().chat().id());
            bot.sendMessage(msg);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("Starting FastBot2 test...");

    if (!FFat.begin(true)) {
        Serial.println("Error mounting FFat.");
    } else {
        Serial.println("FFat mounted successfully.");
    }

    pinMode(LEDR, OUTPUT);
    pinMode(LEDG, OUTPUT);
    pinMode(LEDB, OUTPUT);

    // Initial color: off
    setLedColor(0, 0, 0);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");

    bot.attachUpdate(updateh);
    bot.setToken(BOT_TOKEN);
    bot.setPollMode(fb::Poll::Long, 60000); // long polling is recommended

    fb::Message bootMsg("🌱 ESP32 FastBot2 Test online! Try /config, /unicode, /svg, /dashboard, or /color.", PERSONAL_CHAT_ID);
    fb::Menu mainMenu("");
    mainMenu.newRow();
    mainMenu.addButton("Settings");
    mainMenu.addButton("Set Color");
    mainMenu.addButton("Download Log");
    mainMenu.newRow();
    mainMenu.addButton("Text Graph");
    mainMenu.addButton("Live Dashboard");
    mainMenu.addButton("SVG Graph");
    mainMenu.resize = true;
    bootMsg.setMenu(mainMenu);
    bot.sendMessage(bootMsg);

    // Set the Bot Commands Menu (the '/' button next to the chat bar)
    fb::MyCommands cmds;
    cmds.addCommand("config", "Interactive configuration menu");
    cmds.addCommand("set", "Set target temp via text string");
    cmds.addCommand("color", "Choose an RGB LED color");
    cmds.addCommand("unicode", "Show a text-based sparkline graph");
    cmds.addCommand("dashboard", "Show a live-updating text graph");
    cmds.addCommand("svg", "Show a log-backed SVG graph");
    cmds.addCommand("download", "Download sensor log file");
    bot.setMyCommands(cmds);
}

void loop()
{
    bot.tick();

    // Update the live dashboard every 5 seconds if active
    if (dashboardActive && (millis() - dashboardTimer >= 5000)) {
        dashboardTimer = millis();
        tickMockData(grnhouseTempHistory);
        tickMockData(ambientTempHistory);
        tickMockData(grnhouseHumHistory);
        tickMockData(ambientHumHistory);
        tickMockEventData(sidesHistory);
        
        SensorHistory histories[] = {grnhouseTempHistory, ambientTempHistory, grnhouseHumHistory, ambientHumHistory};
        EventHistory events[] = {sidesHistory};
        
        // editMsgID is passed here so it updates the existing message instead of sending new ones
        sendUnicodeGraph(dashboardChatID, histories, 4, events, 1, dashboardMsgID);
    }
}

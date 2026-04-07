#include "GreenhouseTelegram.h"

GreenhouseTelegramBot::GreenhouseTelegramBot(const String& token, BotOperatingMode mode) 
    : bot(token), operatingMode(mode), targetChatID(0LL), dashboardLiveView(false), logFilePath("grnhs.txt"), dashboardMsgID(0), activeConfigIndex(-1), awaitValue(false),
      logBuffer(nullptr), sensorMetadata(nullptr), numSensors(0), eventMetadata(nullptr), numEvents(0), settings(nullptr), numSettings(0) {}

void GreenhouseTelegramBot::begin(const char* ssid, const char* pass, 
                                  SensorMetadata* sensors, int numS,
                                  EventMetadata* events, int numE,
                                  SettingsParameter* params, int numP) {
    // Initialize PSRAM log buffer
    if (psramInit()) {
        logBuffer = new RingBuffer(100000); // about 8 days if 5 mins per sample, actually 100k samples is 100k*5m=500k minutes = 347 days
        Serial.println("PSRAM init success, LogBuffer allocated.");
    } else {
        Serial.println("PSRAM init failed! Bot will lack history.");
    }                                  
    
    // Start WiFi
    Serial.print("Connecting to WiFi ");
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");

    // Apply metadata
    setSensorMetadata(sensors, numS);
    setEventMetadata(events, numE);
    setSettings(params, numP);

    // Call underlying setup
    setup();
}

void GreenhouseTelegramBot::setup() {
    bot.attachUpdate([this](fb::Update& u) { handleUpdate(u); });
    bot.setPollMode(fb::Poll::Long, 60000);
}

void GreenhouseTelegramBot::tick() {
    bot.tick();
}

void GreenhouseTelegramBot::refreshDashboard() {
    if (dashboardLiveView && dashboardMsgID != 0) {
        dashboardMsgID = sendUnicodeGraph(targetChatID, dashboardMsgID);
    }
}

void GreenhouseTelegramBot::setLogFilePath(const String& path) {
    logFilePath = path;
}

void GreenhouseTelegramBot::setLogBuffer(RingBuffer* buffer) {
    logBuffer = buffer;
}

void GreenhouseTelegramBot::setSensorMetadata(SensorMetadata* metadata, int count) {
    sensorMetadata = metadata;
    numSensors = count;
}

void GreenhouseTelegramBot::setEventMetadata(EventMetadata* metadata, int count) {
    eventMetadata = metadata;
    numEvents = count;
}

void GreenhouseTelegramBot::setSettings(SettingsParameter* p, int count) {
    settings = p;
    numSettings = count;
}

String GreenhouseTelegramBot::formatTime(DateTime dt) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour(), dt.minute());
    return String(buf);
}

void GreenhouseTelegramBot::handleUpdate(fb::Update& u) {
    if (u.isQuery()) {
        handleQuery(u);
    } else if (u.isMessage()) {
        handleMessage(u);
    }
}

void GreenhouseTelegramBot::handleMessage(fb::Update& u) {
    String text = u.message().text();
    fb::ID chatID = u.message().chat().id();

    // Context handler
    if (awaitValue && activeConfigIndex >= 0 && activeConfigIndex < numSettings) {
        float newVal = text.toFloat(); // Fallback if nan
        *(settings[activeConfigIndex].valueRef) = newVal;

        fb::Message msg("✅ " + settings[activeConfigIndex].name + " updated to: " + String(newVal, 1) + settings[activeConfigIndex].unit, chatID);
        bot.sendMessage(msg);

        // Return to settings menu
        sendConfigMainMenu(chatID);
        awaitValue = false;
        return; 
    }

    if (text == "/start") {
        if (operatingMode == MODE_DASHBOARD) {
            targetChatID = chatID;
            sendDashboardMainMenu(chatID);
        } else {
            fb::Message msg("Welcome to Greenhouse Control! Use /dashboard to start.", chatID);
            bot.sendMessage(msg);
        }
    } else if (text == "/dashboard" || text == "Live Dashboard") {
        targetChatID = chatID;
        sendDashboardMainMenu(chatID);
    }
}

void GreenhouseTelegramBot::handleQuery(fb::Update& u) {
    fb::ID chatID = u.query().message().chat().id();
    uint32_t msgID = u.query().message().id();
    String qData = u.query().data();

    // Dashboard Top Level Routes
    if (qData == "menu_controls") {
        sendControlsMenu(chatID, msgID);
        bot.answerCallbackQuery(u.query().id());
        return;
    }
    if (qData == "menu_setpoints") {
        sendConfigMainMenu(chatID, msgID);
        bot.answerCallbackQuery(u.query().id());
        return;
    }
    if (qData == "menu_display") {
        dashboardMsgID = sendUnicodeGraph(chatID, msgID);
        dashboardLiveView = true;
        bot.answerCallbackQuery(u.query().id());
        return;
    }
    if (qData == "menu_svg") {
        sendSvgMenu(chatID, msgID);
        bot.answerCallbackQuery(u.query().id());
        return;
    }
    if (qData == "dl_logs") {
        bool ok = sendLogFile(chatID);
        bot.answerCallbackQuery(u.query().id(), ok ? "Log file sent" : "Log file unavailable");
        return;
    }
    if (qData == "dash_back") {
        sendDashboardMainMenu(chatID, msgID);
        dashboardMsgID = msgID;
        bot.answerCallbackQuery(u.query().id());
        return;
    }
    if (qData == "dl_svg_day" || qData == "dl_svg_week") {
        sendSvgGraph(chatID);
        bot.answerCallbackQuery(u.query().id(), "SVG sent");
        return;
    }
    
    // Setting selections
    if (qData.startsWith("csel_")) {
        int idx = qData.substring(5).toInt();
        if (idx >= 0 && idx < numSettings) {
            activeConfigIndex = idx;
            sendConfigEditMenu(chatID, msgID, idx);
        }
        bot.answerCallbackQuery(u.query().id());
        return;
    }
    // Settings +/- Adjustments
    if (qData.startsWith("cadd_") || qData.startsWith("csub_")) {
        int underscore1 = qData.indexOf('_');
        int underscore2 = qData.lastIndexOf('_');
        if (underscore1 != -1 && underscore2 != -1) {
            String op = qData.substring(0, underscore1);
            int amtIdx = qData.substring(underscore1 + 1, underscore2).toInt();
            int prmIdx = qData.substring(underscore2 + 1).toInt();
            
            float amount = (amtIdx == 3) ? 10.0 : (amtIdx == 2) ? 1.0 : 0.1;
            if (op == "csub") amount = -amount;

            if (prmIdx >= 0 && prmIdx < numSettings) {
                *(settings[prmIdx].valueRef) += amount;
                sendConfigEditMenu(chatID, msgID, prmIdx);
            }
        }
        bot.answerCallbackQuery(u.query().id(), "Value changed");
        return;
    }
    if (qData == "conf_back") {
        if (operatingMode == MODE_DASHBOARD) {
            sendDashboardMainMenu(chatID, msgID);
        } else {
            sendConfigMainMenu(chatID, msgID);
        }
        bot.answerCallbackQuery(u.query().id());
        return;
    }
    
    // Direct Controls Routing (Business Logic Callbacks)
    if (qData.startsWith("evon_")) {
        int idx = qData.substring(5).toInt();
        if (idx >= 0 && idx < numEvents && eventMetadata[idx].controlCallback) {
            eventMetadata[idx].controlCallback(true);
        }
        bot.answerCallbackQuery(u.query().id(), "Command executing");
        return;
    }
    if (qData.startsWith("evoff_")) {
        int idx = qData.substring(6).toInt();
        if (idx >= 0 && idx < numEvents && eventMetadata[idx].controlCallback) {
            eventMetadata[idx].controlCallback(false);
        }
        bot.answerCallbackQuery(u.query().id(), "Command executing");
        return;
    }
}

void GreenhouseTelegramBot::sendDashboardMainMenu(fb::ID chatID, uint32_t editMsgID) {
    String txt = "<b>GH Live Dashboard</b>\n\nChoose an action below:";
    dashboardLiveView = false;
    
    fb::InlineMenu menu("[Controls];[Set Points];[Display]\n"
                        "[Download Logs];[SVG Plot]", 
                        "menu_controls;menu_setpoints;menu_display;"
                        "dl_logs;menu_svg");

    if (editMsgID == 0) {
        fb::Message msg(txt, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.sendMessage(msg);
        dashboardMsgID = bot.lastBotMessage();
    } else {
        fb::TextEdit msg(txt, editMsgID, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.editText(msg);
        dashboardMsgID = editMsgID;
    }
}

void GreenhouseTelegramBot::sendControlsMenu(fb::ID chatID, uint32_t editMsgID) {
    String txt = "<b>Manual Equipment Override</b>";
    dashboardLiveView = false;
    
    String lbls = "";
    String cbs = "";
    
    for (int i = 0; i < numEvents; i++) {
        if (eventMetadata[i].controlCallback != nullptr) {
            lbls += "[" + String(eventMetadata[i].name) + " " + String(eventMetadata[i].onStr) + "];" + 
                    "[" + String(eventMetadata[i].name) + " " + String(eventMetadata[i].offStr) + "]\n";
            cbs += "evon_" + String(i) + ";evoff_" + String(i) + ";";
        }
    }
    
    lbls += "🔙 Back";
    cbs += "dash_back";
    
    fb::InlineMenu menu(lbls, cbs);
    
    fb::TextEdit msg(txt, editMsgID, chatID);
    msg.mode = fb::Message::Mode::HTML;
    msg.setInlineMenu(menu);
    bot.editText(msg);
}

void GreenhouseTelegramBot::sendSvgMenu(fb::ID chatID, uint32_t editMsgID) {
    String txt = "<b>Generate High Def SVG</b>";
    dashboardLiveView = false;
    fb::InlineMenu menu("Download Day\nDownload Week\n🔙 Back", "dl_svg_day;dl_svg_week;dash_back");
    fb::TextEdit msg(txt, editMsgID, chatID);
    msg.mode = fb::Message::Mode::HTML;
    msg.setInlineMenu(menu);
    bot.editText(msg);
}


void GreenhouseTelegramBot::sendConfigMainMenu(fb::ID chatID, uint32_t editMsgID) {
    String txt = "<b>⚙️ Interactive Configuration</b>\nSelect a parameter to adjust:";
    dashboardLiveView = false;
    String lbls = "";
    String cbs = ""; 
    
    for (int i = 0; i < numSettings; i++) {
        lbls += settings[i].icon + " " + settings[i].name + " (" + String(*(settings[i].valueRef), 1) + settings[i].unit + ")";
        cbs += "csel_" + String(i);
        
        if (i < numSettings - 1) {
            lbls += "\n";
            cbs += ";";
        }
    }
    lbls += "\n🔙 Back";
    cbs += ";dash_back";
    
    fb::InlineMenu menu(lbls, cbs);
    
    if (editMsgID == 0) {
        fb::Message msg(txt, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.sendMessage(msg);
    } else {
        fb::TextEdit msg(txt, editMsgID, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.editText(msg);
    }
}

void GreenhouseTelegramBot::sendConfigEditMenu(fb::ID chatID, uint32_t msgID, int paramIndex) {
    if (paramIndex < 0 || paramIndex >= numSettings) return;
    SettingsParameter& p = settings[paramIndex];
    dashboardLiveView = false;

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

uint32_t GreenhouseTelegramBot::sendUnicodeGraph(fb::ID chatID, uint32_t editMsgID) {
    if (!logBuffer || logBuffer->getCount() == 0 || numSensors == 0) return 0;
    
    // For the sparkline dashboard, we show the last N entries (e.g. 48 for sparklines)
    size_t totalCount = logBuffer->getCount();
    size_t count = (totalCount > 48) ? 48 : totalCount; 
    size_t startIndex = totalCount - count;

    int width = 32;

    // Unicode block elements for drawing a vertical sparkline:  ▂▃▄▅▆▇
    const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    float blocksPerReading = (float)width / count;
    
    String title = "Greenhouse Sensor Dashboard";
    String msgText = "<b>" + title + "</b>\n\n";

    // --- Render Binary Event Histories (Fans, Sides, etc) ---
    if (numEvents > 0) {
        msgText += "<u><b>Actuators: </b></u>\n";
        for (int e = 0; e < numEvents; e++) {
            String emjStr = "";
            int blocksAdded = 0;
            
            for (size_t i = 0; i < count; i++) {
                LogEntry entry = logBuffer->get(startIndex + i);
                bool state = entry.*(eventMetadata[e].stateField);
                
                // Emojis are visually 2 characters wide in monospace layout, so we target half the total width blocks
                int targetTotalBlocks = int((i + 1) * (blocksPerReading / 2.0)) - 1; // -1 to prevent overshooting due to emoji width
                int blocksToAdd = targetTotalBlocks - blocksAdded;
                
                for (int j = 0; j < blocksToAdd; j++) {
                    if (state) emjStr += eventMetadata[e].emojiOn;
                    else emjStr += eventMetadata[e].emojiOff;
                    blocksAdded++;
                }
            }
            
            LogEntry lastEntry = logBuffer->get(totalCount - 1);
            bool currentState = lastEntry.*(eventMetadata[e].stateField);
            String stateStr = currentState ? eventMetadata[e].onStr : eventMetadata[e].offStr;
            msgText += eventMetadata[e].name;
            msgText += ": " + stateStr + "\n";
            // Emoji line might not line up perfectly with monospace depending on Telegram OS client, but `<code>` block isn't great for Emojis.
            msgText += "<code>" + emjStr + "</code>\n";
        }
    }

    // Formatting the timestamps
    String startTimeStr = formatTime(logBuffer->get(startIndex).timestamp);
    String midTimeStr = formatTime(logBuffer->get(startIndex + count/2).timestamp);
    String endTimeStr = formatTime(logBuffer->get(totalCount-1).timestamp);
    
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
    for (int h = 0; h < numSensors; h++) {
        float min_val = logBuffer->get(startIndex).*(sensorMetadata[h].valueField);
        float max_val = logBuffer->get(startIndex).*(sensorMetadata[h].valueField);
        float sum_val = 0;
        
        for (size_t i = 0; i < count; i++) {
            float val = logBuffer->get(startIndex + i).*(sensorMetadata[h].valueField);
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
            sum_val += val;
        }
        float avg_val = sum_val / count;

        String sparkline = "";
        int blocksAdded = 0;
        
        for (size_t i = 0; i < count; i++) {
            float val = logBuffer->get(startIndex + i).*(sensorMetadata[h].valueField);
            int block_idx = 0;
            if (max_val > min_val) {
                // Scale reading to an index between 0 and 7
                block_idx = int(((val - min_val) / (max_val - min_val)) * 7);
            }
            
            // Calculate how many total blocks should exist by this point
            int targetTotalBlocks = int((i + 1) * blocksPerReading);
            int blocksToAdd = targetTotalBlocks - blocksAdded;
            
            for (int j = 0; j < blocksToAdd; j++) {
                sparkline += blocks[block_idx];
                blocksAdded++;
            }
        }
        
        String nameStr = String(sensorMetadata[h].name);

        float current_val = logBuffer->get(totalCount-1).*(sensorMetadata[h].valueField);
        String trendStr = "→";
        if (count > 1) {
            float prev_val = logBuffer->get(totalCount-2).*(sensorMetadata[h].valueField);
            if (current_val > prev_val) trendStr = "↗";
            else if (current_val < prev_val) trendStr = "↘";
        }
        
        msgText += "<b>" + nameStr + ":</b> " + String(current_val, 1) + " " + String(sensorMetadata[h].unit) + " " + trendStr + "\n";
        msgText += "<code>" + sparkline + "\n";
        msgText += "min: " + String(min_val, 1);
        msgText += " max: " + String(max_val, 1);
        msgText += " avg: " + String(avg_val, 1);
        msgText += "\n</code>";
    }
    msgText += timeAxis;

    fb::InlineMenu menu("[Controls];[Set Points];[Display]\n"
                        "[Download Logs];[SVG Plot]", 
                        "menu_controls;menu_setpoints;menu_display;"
                        "dl_logs;menu_svg");

    if (editMsgID == 0) {
        fb::Message msg(msgText, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.sendMessage(msg);
        return bot.lastBotMessage();
    } else {
        fb::TextEdit msg(msgText, editMsgID, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.editText(msg);
        return editMsgID;
    }
}

bool GreenhouseTelegramBot::sendLogFile(fb::ID chatID) {
    ::File logFile = SD.open(logFilePath.c_str(), FILE_READ);
    if (!logFile) {
        fb::Message msg("Log file not found on SD: " + logFilePath, chatID);
        bot.sendMessage(msg);
        return false;
    }

    fb::File file(logFilePath, fb::File::Type::document, logFile);
    file.chatID = chatID;
    file.caption = "Greenhouse log export";

    fb::Result result = bot.sendFile(file, true);
    logFile.close();

    if (result.isError()) {
        fb::Message msg("Failed to send log file: " + String(result.getError().c_str()), chatID);
        bot.sendMessage(msg);
        return false;
    }

    return true;
}

void GreenhouseTelegramBot::sendSvgGraph(fb::ID chatID) {
    if (!logBuffer || logBuffer->getCount() == 0 || numSensors == 0) return;
    
    // Default to last 288 records (24h of 5-min intervals)
    size_t totalCount = logBuffer->getCount();
    size_t count = (totalCount > 288) ? 288 : totalCount; 
    size_t startIndex = totalCount - count;

    float min_val = logBuffer->get(startIndex).*(sensorMetadata[0].valueField);
    float max_val = logBuffer->get(startIndex).*(sensorMetadata[0].valueField);
    for (int h = 0; h < numSensors; h++) {
        for (size_t i = 0; i < count; i++) {
            float val = logBuffer->get(startIndex + i).*(sensorMetadata[h].valueField);
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
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
    String title = (numSensors == 1) ? String(sensorMetadata[0].name) : "Combined Sensor";
    svg += "  <text x=\"" + String(width / 2) + "\" y=\"20\" class=\"title\" text-anchor=\"middle\">" + title + " History</text>\n";

    // Draw Axes
    svg += "  <line x1=\"" + String(marginX) + "\" y1=\"" + String(marginYTop) + "\" x2=\"" + String(marginX) + "\" y2=\"" + String(height - marginYBottom) + "\" stroke=\"#ccc\" stroke-width=\"1.5\" />\n"; // Y Axis
    svg += "  <line x1=\"" + String(marginX) + "\" y1=\"" + String(height - marginYBottom) + "\" x2=\"" + String(width - 20) + "\" y2=\"" + String(height - marginYBottom) + "\" stroke=\"#ccc\" stroke-width=\"1.5\" />\n"; // X Axis

    // Y Axis Labels
    svg += "  <text x=\"" + String(marginX - 5) + "\" y=\"" + String(marginYTop + 4) + "\" class=\"axis\" text-anchor=\"end\">" + String(max_val, 1) + "</text>\n";
    svg += "  <text x=\"" + String(marginX - 5) + "\" y=\"" + String(height - marginYBottom + 4) + "\" class=\"axis\" text-anchor=\"end\">" + String(min_val, 1) + "</text>\n";
    
    // X-Axis Time Labels (Drawn once for the primary timescale)
    for (size_t i = 0; i < count; i++) {
        if (i % 12 == 0 || i == count - 1) { // Sparsify time labels
            int x = marginX + (i * pointSpacing) + (pointSpacing / 2);
            svg += "  <text x=\"" + String(x) + "\" y=\"" + String(height - marginYBottom + 16) + "\" class=\"axis\" text-anchor=\"middle\">" + formatTime(logBuffer->get(startIndex + i).timestamp) + "</text>\n";
        }
    }

    // Draw Blocks for Events (ON State)
    if (numEvents > 0) {
        for (int e = 0; e < numEvents; e++) {
            String color = eventMetadata[e].color ? String(eventMetadata[e].color) : "#000000";
            int yEvent = height - marginYBottom + marginYBottomBase + (e * eventRowHeight) - 8;
            
            // Draw Event Title on Y Axis
            svg += "  <text x=\"" + String(marginX - 5) + "\" y=\"" + String(yEvent + 8) + "\" class=\"axis\" text-anchor=\"end\">" + String(eventMetadata[e].name) + "</text>\n";
            
            // Draw Rectangles for Active States
            for (size_t i = 0; i < count; i++) {
                if (logBuffer->get(startIndex + i).*(eventMetadata[e].stateField)) {
                    int xRect = marginX + (i * pointSpacing);
                    svg += "  <rect x=\"" + String(xRect) + "\" y=\"" + String(yEvent) + "\" width=\"" + String(pointSpacing) + "\" height=\"10\" fill=\"" + color + "\" opacity=\"0.6\" />\n";
                }
            }
        }
    }

    // Start drawing data lines for each history
    for (int h = 0; h < numSensors; h++) {
        String color = sensorMetadata[h].color ? String(sensorMetadata[h].color) : "#000000";
        
        // Draw Legend for this line
        int legendX = marginX + 10 + (h * 80);
        svg += "  <text x=\"" + String(legendX) + "\" y=\"40\" class=\"legend\" fill=\"" + color + "\">" + String(sensorMetadata[h].name) + "</text>\n";

        // Line
        svg += "  <polyline fill=\"none\" stroke=\"" + color + "\" stroke-width=\"2\" points=\"";
        for (size_t i = 0; i < count; i++) {
            float val = logBuffer->get(startIndex + i).*(sensorMetadata[h].valueField);
            int x = marginX + (i * pointSpacing) + (pointSpacing / 2);
            int y = height - marginYBottom - int(((val - min_val) / (max_val - min_val)) * graphHeight);
            svg += String(x) + "," + String(y) + " ";
        }
        svg += "\" />\n";
        
        // Draw points and values
        for (size_t i = 0; i < count; i++) {
            float val = logBuffer->get(startIndex + i).*(sensorMetadata[h].valueField);
            int x = marginX + (i * pointSpacing) + (pointSpacing / 2);
            int y = height - marginYBottom - int(((val - min_val) / (max_val - min_val)) * graphHeight);
            
            // Value point
            // For large datasets, limit drawing labels to prevent clutter
            if (i % 12 == 0 || i == count - 1) { 
                svg += "  <circle cx=\"" + String(x) + "\" cy=\"" + String(y) + "\" r=\"3.5\" fill=\"" + color + "\" />\n";
                int yOffset = (h % 2 == 0) ? -8 : 14; 
                svg += "  <text x=\"" + String(x) + "\" y=\"" + String(y + yOffset) + "\" class=\"val\" fill=\"" + color + "\" text-anchor=\"middle\">" + String(val, 1) + "</text>\n";
            }
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
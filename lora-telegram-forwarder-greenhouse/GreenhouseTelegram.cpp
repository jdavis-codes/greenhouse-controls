#include "GreenhouseTelegram.h"

#include <ctype.h>
#include <stdlib.h>

GreenhouseTelegramBot::GreenhouseTelegramBot(const String& token, BotOperatingMode mode) 
    : bot(token), operatingMode(mode), targetChatID(0LL), dashboardLiveView(false), logFilePath("grnhs.txt"), dashboardMsgID(0), activeConfigIndex(-1),
    logBuffer(nullptr), sensorMetadata(nullptr), numSensors(0), eventMetadata(nullptr), numEvents(0), settings(nullptr), numSettings(0),
    deviceBootMillis(0), lastLoRaRxMillis(0), linkConnectedSinceMillis(0), lastRssi(0), lastSnr(0) {}

void GreenhouseTelegramBot::begin(SensorMetadata* sensors, int numS,
                                  EventMetadata* events, int numE,
                                  SettingsParameter* params, int numP) {
    deviceBootMillis = millis();

    // Initialize PSRAM log buffer
    if (psramInit()) {
        logBuffer = new RingBuffer(100000); // about 8 days if 5 mins per sample, actually 100k samples is 100k*5m=500k minutes = 347 days
        Serial.println("PSRAM init success, LogBuffer allocated.");
    } else {
        Serial.println("PSRAM init failed! Bot will lack history.");
    }                                  

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
    installBotCommands();
}

void GreenhouseTelegramBot::tick() {
    bot.tick();
}

void GreenhouseTelegramBot::refreshDashboard() {
    if (dashboardLiveView && dashboardMsgID != 0) {
        cachedDashboardBody = ""; // Invalidate cache so fresh data is rendered
        dashboardMsgID = sendUnicodeGraph(targetChatID, dashboardMsgID);
    }
}

void GreenhouseTelegramBot::updateLinkMetrics(int rssi, int snr) {
    bool wasConnected = isLinkConnected();
    lastRssi = rssi;
    lastSnr = snr;
    lastLoRaRxMillis = millis();
    if (!wasConnected || linkConnectedSinceMillis == 0) {
        linkConnectedSinceMillis = lastLoRaRxMillis;
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

void GreenhouseTelegramBot::installBotCommands() {
    fb::MyCommands cmds;
    cmds.addCommand("dashboard", "Show the current greenhouse status");
    cmds.addCommand("controls", "List manual control commands");
    cmds.addCommand("control", "Send /control <device> <state>");
    cmds.addCommand("config", "Show or set /config <parameter> <value>");
    cmds.addCommand("graph", "Send the SVG history graph");
    cmds.addCommand("download", "Download the current log file");
    cmds.addCommand("help", "Show command formatting help");
    cmds.addCommand("commands", "Show the full command list");
    bot.setMyCommands(cmds, false);
}

String GreenhouseTelegramBot::formatTime(DateTime dt) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour(), dt.minute());
    return String(buf);
}

String GreenhouseTelegramBot::formatDuration(unsigned long elapsedMs) const {
    unsigned long totalSeconds = elapsedMs / 1000;
    unsigned long hours = totalSeconds / 3600;
    unsigned long minutes = (totalSeconds % 3600) / 60;
    unsigned long seconds = totalSeconds % 60;

    char buf[20];
    if (hours >= 100) {
        snprintf(buf, sizeof(buf), "%luh %02lum", hours, minutes);
    } else {
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    }
    return String(buf);
}

bool GreenhouseTelegramBot::isLinkConnected() const {
    if (lastLoRaRxMillis == 0) return false;
    return (millis() - lastLoRaRxMillis) <= 45000;
}

const char* GreenhouseTelegramBot::getLinkQualityLabel() const {
    if (!isLinkConnected()) return "No Signal";
    if (lastRssi >= -80 && lastSnr >= 8) return "Excellent";
    if (lastRssi >= -95 && lastSnr >= 4) return "Good     ";
    if (lastRssi >= -110 && lastSnr >= 0) return "Fair    ";
    return "Poor";
}

const char* GreenhouseTelegramBot::getLinkQualityBar() const {
    if (!isLinkConnected()) return "[-----]";
    if (lastRssi >= -80 && lastSnr >= 8) return "[#####]";
    if (lastRssi >= -95 && lastSnr >= 4) return "[####-]";
    if (lastRssi >= -110 && lastSnr >= 0) return "[###--]";
    return "[##---]";
}

String GreenhouseTelegramBot::buildDashboardStatusHeader() const {
    String header = "<b>Greenhouse Sensor Dashboard</b>\n";
    header += "<code>";
    header += "🛜 ";
    header += getLinkQualityLabel();
    header += " | RSSI: ";
    if (isLinkConnected()) {
        header += String(lastRssi);
        header += " SNR: ";
        header += String(lastSnr);
    } else {
        header += "-- dBm | SNR -- dB";
    }
    header += "\n";
    header += "Conn ";
    header += isLinkConnected() && linkConnectedSinceMillis ? formatDuration(millis() - linkConnectedSinceMillis) : String("00:00:00");
    header += " | Uptime ";
    header += formatDuration(millis() - deviceBootMillis);
    header += "</code>\n\n";
    return header;
}

String GreenhouseTelegramBot::buildGuideText() const {
    return String(
        "<b>User Guide</b>\n"
        "<i>Quick reference for the greenhouse bot.</i>\n\n"
        "<b>/dashboard</b>\n"
        "Shows the current dashboard snapshot with link health, actuator history, and sensor sparklines.\n\n"
        "<b>/controls</b>\n"
        "Lists manual control commands. Use <code>/control fan on</code>, <code>/control sides down</code>, or <code>/control irrigation off</code>.\n\n"
        "<b>/config</b>\n"
        "Shows current setpoints and usage. Set values with <code>/config temp1 82.5</code> or <code>/config moist 55</code>.\n\n"
        "<b>/graph</b>\n"
        "Generates a high-resolution SVG plot from recent history and sends it as a file.\n\n"
        "<b>/download</b>\n"
        "Sends the current SD log file to Telegram for review or backup.\n\n"
        "<b>Status Header</b>\n"
        "Displays LoRa quality, RSSI, SNR, connection uptime, and device uptime.\n\n"
        "<b>Group Chats</b>\n"
        "Commands with bot mentions like <code>/dashboard@YourBotName</code> are accepted automatically.");
}

String GreenhouseTelegramBot::buildCommandsText() const {
    return String(
        "<b>Available Commands</b>\n"
        "<code>/dashboard</code> or <code>/display</code> - show current dashboard snapshot\n"
        "<code>/controls</code> - list manual control commands\n"
        "<code>/control &lt;device&gt; &lt;state&gt;</code> - control fan, sides, or irrigation\n"
        "<code>/config</code> - show current parameters and usage\n"
        "<code>/config &lt;parameter&gt; &lt;value&gt;</code> - set a configuration value\n"
        "<code>/graph</code> - generate and send the SVG plot\n"
        "<code>/download</code> - send the log file\n"
        "<code>/help</code> - formatting help and workflow notes\n"
        "<code>/commands</code> - show this command list");
}

String GreenhouseTelegramBot::buildConfigText() const {
    String text = "<b>Config Parameters</b>\n";
    text += "Use <code>/config &lt;parameter&gt; &lt;value&gt;</code>\n\n";
    for (int i = 0; i < numSettings; i++) {
        String key = settings[i].key;
        key.toLowerCase();
        text += "<code>" + key + "</code> - ";
        text += settings[i].name + " = ";
        text += String(*(settings[i].valueRef), 1) + settings[i].unit + "\n";
    }
    text += "\nExamples:\n";
    text += "<code>/config temp1 82.5</code>\n";
    text += "<code>/config moist 55</code>";
    return text;
}

String GreenhouseTelegramBot::buildControlsText() const {
    String text = "<b>Manual Controls</b>\n";
    text += "Use <code>/control &lt;device&gt; &lt;state&gt;</code>\n\n";
    for (int i = 0; i < numEvents; i++) {
        String name = eventMetadata[i].name;
        String device = normalizeToken(name);
        String onState = eventMetadata[i].onStr;
        String offState = eventMetadata[i].offStr;
        onState.toLowerCase();
        offState.toLowerCase();
        text += "<code>/control " + device + " " + onState + "</code> or ";
        text += "<code>/control " + device + " " + offState + "</code>\n";
    }
    text += "\nAliases: <code>fan</code>, <code>sides</code>, <code>irrigation</code>, <code>water</code>.";
    return text;
}

String GreenhouseTelegramBot::extractCommandName(const String& text, String& args) const {
    String trimmed = text;
    trimmed.trim();
    args = "";
    if (!trimmed.length()) return String();

    int spaceIdx = trimmed.indexOf(' ');
    String command = (spaceIdx == -1) ? trimmed : trimmed.substring(0, spaceIdx);
    if (spaceIdx != -1) {
        args = trimmed.substring(spaceIdx + 1);
        args.trim();
    }

    if (!command.startsWith("/")) return command;

    int mentionIdx = command.indexOf('@');
    if (mentionIdx != -1) {
        command = command.substring(0, mentionIdx);
    }
    command.toLowerCase();
    return command;
}

String GreenhouseTelegramBot::normalizeToken(const String& text) const {
    String token;
    token.reserve(text.length());
    for (unsigned int i = 0; i < text.length(); i++) {
        char ch = text[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            token += (char)tolower(ch);
        }
    }
    return token;
}

int GreenhouseTelegramBot::findSettingIndex(const String& token) const {
    String normalized = normalizeToken(token);
    for (int i = 0; i < numSettings; i++) {
        String key = normalizeToken(settings[i].key);
        String name = normalizeToken(settings[i].name);
        if (normalized == key || normalized == name || name.indexOf(normalized) >= 0) {
            return i;
        }
    }
    return -1;
}

int GreenhouseTelegramBot::findEventIndex(const String& token) const {
    String normalized = normalizeToken(token);
    for (int i = 0; i < numEvents; i++) {
        String name = normalizeToken(eventMetadata[i].name);
        if (normalized == name || name.indexOf(normalized) >= 0) {
            return i;
        }
    }
    if (normalized == "water") {
        for (int i = 0; i < numEvents; i++) {
            if (normalizeToken(eventMetadata[i].name).indexOf("irrigation") >= 0) return i;
        }
    }
    return -1;
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
    auto chat = u.message().chat();
    fb::ID chatID = chat.id();

    auto chatType = chat.type();
    const char* chatTypeStr = "unknown";
    switch (chatType) {
        case decltype(chatType)::privateChat:
            chatTypeStr = "private";
            break;
        case decltype(chatType)::group:
            chatTypeStr = "group";
            break;
        case decltype(chatType)::supergroup:
            chatTypeStr = "supergroup";
            break;
        case decltype(chatType)::channel:
            chatTypeStr = "channel";
            break;
    }

    Serial.println("Received message: " + text);
    Serial.println("Chat properties:");
    Serial.println("  id: " + String(chat.id().c_str()));
    Serial.println("  type: " + String(chatTypeStr));
    Serial.println("  title: " + String(chat.title().c_str()));
    Serial.println("  username: " + String(chat.username().c_str()));
    Serial.println("  first_name: " + String(chat.firstName().c_str()));
    Serial.println("  last_name: " + String(chat.lastName().c_str()));
    Serial.println("  description: " + String(chat.description().c_str()));
    Serial.println(String("  is_forum: ") + (chat.isForum() ? "true" : "false"));

    String args;
    String command = extractCommandName(text, args);
    bool isGroupChat = chatType == decltype(chatType)::group || chatType == decltype(chatType)::supergroup;

    if (operatingMode == MODE_DASHBOARD) {
        if (command == "/start") {
            targetChatID = chatID;
            sendDashboardMainMenu(chatID);
        } else if (command == "/dashboard" || command == "/display" || text == "Live Dashboard") {
            targetChatID = chatID;
            sendDashboardMainMenu(chatID);
        }
        return;
    }

    if (isGroupChat && !command.startsWith("/")) {
        return;
    }

    if (command == "/start") {
        fb::Message msg("Welcome to Greenhouse Control. Use /commands for the command list or /help for usage examples.", chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.sendMessage(msg, false);
        return;
    }
    if (command == "/commands") {
        fb::Message msg(buildCommandsText(), chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.sendMessage(msg, false);
        return;
    }
    if (command == "/help" || command == "/guide") {
        fb::Message msg(buildGuideText(), chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.sendMessage(msg, false);
        return;
    }
    if (command == "/dashboard" || command == "/display" || command == "/status") {
        targetChatID = chatID;
        sendUnicodeGraph(chatID);
        return;
    }
    if (command == "/graph") {
        sendSvgGraph(chatID);
        return;
    }
    if (command == "/download") {
        sendLogFile(chatID);
        return;
    }
    if (command == "/controls") {
        fb::Message msg(buildControlsText(), chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.sendMessage(msg, false);
        return;
    }
    if (command == "/control") {
        int splitIdx = args.indexOf(' ');
        if (splitIdx == -1) {
            fb::Message msg(buildControlsText(), chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);
            return;
        }

        String deviceToken = args.substring(0, splitIdx);
        String stateToken = args.substring(splitIdx + 1);
        deviceToken.trim();
        stateToken.trim();
        stateToken = normalizeToken(stateToken);

        int eventIdx = findEventIndex(deviceToken);
        if (eventIdx < 0 || !eventMetadata[eventIdx].controlCallback) {
            bot.sendMessage(fb::Message("Unknown control. Use /controls to see valid devices.", chatID), false);
            return;
        }

        String onToken = normalizeToken(eventMetadata[eventIdx].onStr);
        String offToken = normalizeToken(eventMetadata[eventIdx].offStr);
        bool state = false;
        bool valid = false;
        if (stateToken == "1" || stateToken == "on" || stateToken == "true" || stateToken == onToken) {
            state = true;
            valid = true;
        } else if (stateToken == "0" || stateToken == "off" || stateToken == "false" || stateToken == offToken) {
            state = false;
            valid = true;
        }

        if (!valid) {
            bot.sendMessage(fb::Message("Unknown control state. Use on/off, 1/0, or the device labels shown by /controls.", chatID), false);
            return;
        }

        eventMetadata[eventIdx].controlCallback(state);
        bot.sendMessage(fb::Message(String("Command sent: ") + eventMetadata[eventIdx].name + " -> " + (state ? eventMetadata[eventIdx].onStr : eventMetadata[eventIdx].offStr), chatID), false);
        return;
    }
    if (command == "/config") {
        if (!args.length()) {
            fb::Message msg(buildConfigText(), chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);
            return;
        }

        int splitIdx = args.indexOf(' ');
        if (splitIdx == -1) {
            fb::Message msg(buildConfigText(), chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);
            return;
        }

        String paramToken = args.substring(0, splitIdx);
        String valueToken = args.substring(splitIdx + 1);
        paramToken.trim();
        valueToken.trim();

        int settingIdx = findSettingIndex(paramToken);
        if (settingIdx < 0) {
            bot.sendMessage(fb::Message("Unknown parameter. Use /config to see available keys.", chatID), false);
            return;
        }

        char* endPtr = nullptr;
        float value = strtof(valueToken.c_str(), &endPtr);
        if (endPtr == valueToken.c_str() || (endPtr && *endPtr != '\0')) {
            bot.sendMessage(fb::Message("Invalid value. Example: /config temp1 82.5", chatID), false);
            return;
        }

        *(settings[settingIdx].valueRef) = value;
        if (settingChangedCb) settingChangedCb(settings[settingIdx].key, value);

        fb::Message msg("✅ " + settings[settingIdx].name + " updated to " + String(value, 1) + settings[settingIdx].unit, chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.sendMessage(msg, false);
        return;
    }

    if (command.startsWith("/")) {
        bot.sendMessage(fb::Message("Unknown command. Use /commands to see the available commands.", chatID), false);
    }
}

void GreenhouseTelegramBot::handleQuery(fb::Update& u) {
    fb::ID chatID = u.query().message().chat().id();
    uint32_t msgID = u.query().message().id();
    String qData = u.query().data();

    // Dashboard Top Level Routes
    if (qData == "menu_controls") {
        bot.answerCallbackQuery(u.query().id());
        sendControlsMenu(chatID, msgID);
        return;
    }
    if (qData == "menu_setpoints") {
        bot.answerCallbackQuery(u.query().id());
        sendConfigMainMenu(chatID, msgID);
        return;
    }
    if (qData == "menu_display") {
        bot.answerCallbackQuery(u.query().id());
        dashboardMsgID = sendUnicodeGraph(chatID, msgID);
        dashboardLiveView = true;
        return;
    }
    if (qData == "menu_svg") {
        bot.answerCallbackQuery(u.query().id());
        sendSvgMenu(chatID, msgID);
        return;
    }
    if (qData == "menu_guide") {
        bot.answerCallbackQuery(u.query().id());
        sendGuideMenu(chatID, msgID);
        return;
    }
    if (qData == "dl_logs") {
        bool ok = sendLogFile(chatID);
        bot.answerCallbackQuery(u.query().id(), ok ? "Log file sent" : "Log file unavailable");
        return;
    }
    if (qData == "dash_back") {
        bot.answerCallbackQuery(u.query().id());
        sendDashboardMainMenu(chatID, msgID);
        dashboardMsgID = msgID;
        return;
    }
    if (qData == "dl_svg_day" || qData == "dl_svg_week") {
        bot.answerCallbackQuery(u.query().id(), "SVG sent");
        sendSvgGraph(chatID);
        return;
    }
    
    // Setting selections
    if (qData.startsWith("csel_")) {
        bot.answerCallbackQuery(u.query().id());
        int idx = qData.substring(5).toInt();
        if (idx >= 0 && idx < numSettings) {
            activeConfigIndex = idx;
            sendConfigEditMenu(chatID, msgID, idx);
        }
        return;
    }
    // Settings +/- Adjustments
    if (qData.startsWith("cadd_") || qData.startsWith("csub_")) {
        bot.answerCallbackQuery(u.query().id(), "Value changed");
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
                if (settingChangedCb) settingChangedCb(settings[prmIdx].key, *(settings[prmIdx].valueRef));
                sendConfigEditMenu(chatID, msgID, prmIdx);
            }
        }
        return;
    }
    if (qData == "conf_back") {
        bot.answerCallbackQuery(u.query().id());
        sendConfigMainMenu(chatID, msgID);
        return;
    }
    
    // Direct Controls Routing (Business Logic Callbacks)
    if (qData.startsWith("evon_")) {
        bot.answerCallbackQuery(u.query().id(), "Command executing");
        int idx = qData.substring(5).toInt();
        if (idx >= 0 && idx < numEvents && eventMetadata[idx].controlCallback) {
            eventMetadata[idx].controlCallback(true);
        }
        return;
    }
    if (qData.startsWith("evoff_")) {
        bot.answerCallbackQuery(u.query().id(), "Command executing");
        int idx = qData.substring(6).toInt();
        if (idx >= 0 && idx < numEvents && eventMetadata[idx].controlCallback) {
            eventMetadata[idx].controlCallback(false);
        }
        return;
    }
}

void GreenhouseTelegramBot::sendDashboardMainMenu(fb::ID chatID, uint32_t editMsgID) {
    dashboardMsgID = sendUnicodeGraph(chatID, editMsgID);
    dashboardLiveView = true;
    targetChatID = chatID;
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
    bot.editText(msg, false);
}

void GreenhouseTelegramBot::sendSvgMenu(fb::ID chatID, uint32_t editMsgID) {
    String txt = "<b>Generate High Def SVG</b>";
    dashboardLiveView = false;
    fb::InlineMenu menu("Download Day\nDownload Week\n🔙 Back", "dl_svg_day;dl_svg_week;dash_back");
    fb::TextEdit msg(txt, editMsgID, chatID);
    msg.mode = fb::Message::Mode::HTML;
    msg.setInlineMenu(menu);
    bot.editText(msg, false);
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
        bot.sendMessage(msg, false);
    } else {
        fb::TextEdit msg(txt, editMsgID, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.editText(msg, false);
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
    bot.editText(msg, false);
}

void GreenhouseTelegramBot::sendGuideMenu(fb::ID chatID, uint32_t editMsgID) {
    dashboardLiveView = false;

    String txt =
        "<b>User Guide</b>\n"
        "<i>Quick reference for the greenhouse dashboard.</i>\n\n"
        "<b>Display</b>\n"
        "Shows live link health, actuator history, and sensor sparklines. Use this as the main status view.\n\n"
        "<b>Controls</b>\n"
        "Sends manual LoRa commands for fan, sides, and irrigation. Use for testing or temporary overrides.\n\n"
        "<b>Set Points</b>\n"
        "Adjusts target temperatures and moisture thresholds. Tap a parameter, then use +/- buttons to change it.\n\n"
        "<b>Download Logs</b>\n"
        "Sends the current SD log file to Telegram for offline review or backup.\n\n"
        "<b>SVG Plot</b>\n"
        "Generates a high-resolution plot from recent history and sends it as a file. Best for detailed trend review.\n\n"
        "<b>Status Header</b>\n"
        "The top line shows LoRa quality, RSSI, SNR, connection uptime, and device uptime. If no packets arrive for a while, the link is marked disconnected.\n\n"
        "<b>Typical Workflow</b>\n"
        "Check Display for health, use Set Points for threshold changes, use Controls for manual tests, and export logs or SVG when you need history.";

    fb::InlineMenu menu("🔙 Back", "dash_back");

    fb::TextEdit msg(txt, editMsgID, chatID);
    msg.mode = fb::Message::Mode::HTML;
    msg.setInlineMenu(menu);
    bot.editText(msg, false);
}

uint32_t GreenhouseTelegramBot::sendUnicodeGraph(fb::ID chatID, uint32_t editMsgID) {
    if (!logBuffer || numSensors == 0) return editMsgID; // Can't do anything without struct refs

    String msgText = buildDashboardStatusHeader();
    if (!cachedDashboardBody.isEmpty() && logBuffer->getCount() > 0) {
        msgText += cachedDashboardBody;
    } else if (logBuffer->getCount() == 0) {
        msgText += "<i>Waiting for live sensor data over LoRa...</i>";
    } else {
        // For the sparkline dashboard, we show the last N entries (e.g. 48 for sparklines)
        size_t totalCount = logBuffer->getCount();
        size_t count = (totalCount > 48) ? 48 : totalCount; 
        size_t startIndex = totalCount - count;

        int width = 32;

        // Unicode block elements for drawing a vertical sparkline:  ▂▃▄▅▆▇
        const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        float blocksPerReading = (float)width / count;
        
        String bodyText;

        // --- Render Binary Event Histories (Fans, Sides, etc) ---
        if (numEvents > 0) {
            bodyText += "<u><b>Actuators: </b></u>\n";
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
                bodyText += eventMetadata[e].name;
                bodyText += ": " + stateStr + "\n";
                // Emoji line might not line up perfectly with monospace depending on Telegram OS client, but `<code>` block isn't great for Emojis.
                bodyText += "<code>" + emjStr + "</code>\n";
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

        bodyText += timeAxis;


        // --- Render Analog Sensor Histories ---
        bodyText += "\n<u><b>Sensor Readings: </b></u>\n";
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

            // add trend arrow based on last 2 readings

            float current_val = logBuffer->get(totalCount-1).*(sensorMetadata[h].valueField);
            String trandArrow = "→";
            if (count > 1) {
                float prev_val = logBuffer->get(totalCount-2).*(sensorMetadata[h].valueField);
                if (current_val > prev_val) trandArrow = "↗";
                else if (current_val < prev_val) trandArrow = "↘";
            }
            
            bodyText += "<b>" + trandArrow + nameStr + ":</b> " + String(current_val, 1) + " " + String(sensorMetadata[h].unit) + "\n";
            bodyText += "<code>" + sparkline + "\n";
            bodyText += "min: " + String(min_val, 1);
            bodyText += " max: " + String(max_val, 1);
            bodyText += " avg: " + String(avg_val, 1);
            bodyText += "\n</code>";
        }
        bodyText += timeAxis;
        cachedDashboardBody = bodyText;
        msgText += bodyText;
    } // end logBuffer parsing

    fb::InlineMenu menu("Controls;Set Points;Display\n"
                        "Download;Graph;Guide", 
                        "menu_controls;menu_setpoints;menu_display;"
                        "dl_logs;menu_svg;menu_guide");

    if (editMsgID == 0) {
        fb::Message msg(msgText, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.sendMessage(msg);  // wait=true needed to get lastBotMessage()
        return bot.lastBotMessage();
    } else {
        fb::TextEdit msg(msgText, editMsgID, chatID);
        msg.mode = fb::Message::Mode::HTML;
        msg.setInlineMenu(menu);
        bot.editText(msg, false);
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
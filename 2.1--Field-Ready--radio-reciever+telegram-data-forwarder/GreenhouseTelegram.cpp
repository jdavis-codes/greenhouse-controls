#include "GreenhouseTelegram.h"

#include <ctype.h>
#include <stdlib.h>

GreenhouseTelegramBot::GreenhouseTelegramBot(const String& token, BotOperatingMode mode) 
    : bot(token), operatingMode(mode), logFilePath("grnhs.txt"), activeConfigIndex(-1),
    pendingConfigChatID(""), logBuffer(nullptr), sensorMetadata(nullptr), numSensors(0), eventMetadata(nullptr), numEvents(0), settings(nullptr), numSettings(0),
    sensorAlerts(nullptr), eventAlerts(nullptr), pendingAlertSensorIndex(-1), pendingAlertThresholdType(ALERT_THRESHOLD_NONE), pendingAlertChatID(""),
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
    cachedDashboardBody = ""; // Invalidate cache so fresh data is rendered

    if (personalChatSession.dashboardLiveView && personalChatSession.dashboardMsgID != 0 && personalChatSession.chatID.length()) {
        personalChatSession.dashboardMsgID = sendUnicodeGraph(fb::ID(personalChatSession.chatID), personalChatSession.dashboardMsgID);
    }
    if (groupChatSession.dashboardLiveView && groupChatSession.dashboardMsgID != 0 && groupChatSession.chatID.length()) {
        groupChatSession.dashboardMsgID = sendUnicodeGraph(fb::ID(groupChatSession.chatID), groupChatSession.dashboardMsgID);
    }
}

void GreenhouseTelegramBot::sendTextToChat(const char* chatID, const String& text, bool html) {
    if (!chatID || !chatID[0]) return;

    fb::Message msg(text, fb::ID(chatID));
    if (html) {
        msg.mode = fb::Message::Mode::HTML;
    }
    bot.sendMessage(msg, false);
}

void GreenhouseTelegramBot::configureKnownChats(const char* personalChatID, const char* groupChatID) {
    personalChatSession.chatID = personalChatID ? String(personalChatID) : String();
    groupChatSession.chatID = groupChatID ? String(groupChatID) : String();
    personalChatSession.mode = operatingMode;
    groupChatSession.mode = operatingMode;
}

void GreenhouseTelegramBot::sendBootAnnouncements() {
    if (personalChatSession.chatID.length()) {
        sendTextToChat(personalChatSession.chatID.c_str(), buildBootText(personalChatSession.mode), true);
    }
    if (groupChatSession.chatID.length() && groupChatSession.chatID != personalChatSession.chatID) {
        sendTextToChat(groupChatSession.chatID.c_str(), buildBootText(groupChatSession.mode), true);
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
    if (sensorAlerts) {
        delete[] sensorAlerts;
        sensorAlerts = nullptr;
    }
    if (count > 0) {
        sensorAlerts = new SensorAlertConfig[count];
    }
}

void GreenhouseTelegramBot::setEventMetadata(EventMetadata* metadata, int count) {
    eventMetadata = metadata;
    numEvents = count;
    if (eventAlerts) {
        delete[] eventAlerts;
        eventAlerts = nullptr;
    }
    if (count > 0) {
        eventAlerts = new EventAlertConfig[count];
    }
}

void GreenhouseTelegramBot::setSettings(SettingsParameter* p, int count) {
    settings = p;
    numSettings = count;
}

void GreenhouseTelegramBot::broadcastAlertMessage(const String& text, bool html) {
    notifyAlertChats(text);
}

bool GreenhouseTelegramBot::syncSettingValue(const char* key, float value) {
    if (!key) return false;

    int settingIdx = findSettingIndex(key);
    if (settingIdx < 0) return false;

    *(settings[settingIdx].valueRef) = value;
    return true;
}

void GreenhouseTelegramBot::syncSensorAlertConfig(int sensorIdx, bool lowEnabled, float lowThreshold, bool highEnabled, float highThreshold) {
    if (!sensorAlerts || sensorIdx < 0 || sensorIdx >= numSensors) return;

    sensorAlerts[sensorIdx].lowEnabled = lowEnabled;
    sensorAlerts[sensorIdx].lowThreshold = lowThreshold;
    sensorAlerts[sensorIdx].lowActive = false;
    sensorAlerts[sensorIdx].highEnabled = highEnabled;
    sensorAlerts[sensorIdx].highThreshold = highThreshold;
    sensorAlerts[sensorIdx].highActive = false;
}

void GreenhouseTelegramBot::syncEventAlertConfig(int eventIdx, bool enabled) {
    if (!eventAlerts || eventIdx < 0 || eventIdx >= numEvents) return;

    eventAlerts[eventIdx].enabled = enabled;
    eventAlerts[eventIdx].lastStateKnown = false;
}

void GreenhouseTelegramBot::installBotCommands() {
    fb::MyCommands cmds;
    cmds.addCommand("dashboard", "live from New York, it's the dashboard!");
    cmds.addCommand("chat", "Switch this chat to command mode");
    cmds.addCommand("snapshot", "state of the union!");
    cmds.addCommand("control", "Open the control menu or send /control <device> <state>");
    cmds.addCommand("config", "Show or set /config <parameter> <value>");
    cmds.addCommand("alert", "Open sensor and equipment alert settings");
    cmds.addCommand("cancel", "Cancel a pending setpoint reply");
    cmds.addCommand("graph", "Send the SVG history graph");
    cmds.addCommand("download", "Download the current log file");
    cmds.addCommand("help", "Show command formatting help");
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
        "<i>Quick reference for the current chat.</i>\n\n"
        "<b>Two Chat Modes</b>\n"
        "Each Telegram chat keeps its own mode. Your personal chat can stay in command mode while a group chat stays on the live dashboard.\n\n"
        "<b>/dashboard</b>\n"
        "Switches only this chat into dashboard mode and opens the live inline dashboard with status, controls, set points, logs, and plots.\n\n"
        "<b>/chat</b>\n"
        "Switches only this chat into chat mode for slash commands, reply-based config updates, and short status messages.\n\n"
        "<b>/snapshot</b>\n"
        "Shows the latest readings, trend arrows, and actuator states in one compact text snapshot.\n\n"
        "<b>/control</b>\n"
        "With no arguments, opens the manual control menu in chat mode. You can also send commands directly, for example <code>/control fan on</code>, <code>/control sides down</code>, or <code>/control irrigation off</code>.\n\n"
        "<b>/config</b>\n"
        "Opens the set point selector in chat mode. Tap a parameter, then reply with a new value, or set it directly with <code>/config temp1 82.5</code> or <code>/config moist 55</code>.\n\n"
        "<b>/alert</b>\n"
        "Opens alert setup. Sensor alerts can watch for values above or below a threshold, and equipment alerts can be turned on or off for fan, roller sides, and irrigation.\n\n"
        "<b>/cancel</b>\n"
        "Cancels a pending setpoint or alert threshold reply after you select an option from the menu.\n\n"
        "<b>/status</b>\n"
        "Sends the full Unicode dashboard summary in chat mode when you want the richer text view without switching the chat to dashboard mode.\n\n"
        "<b>/graph</b>\n"
        "Generates a high-resolution SVG plot from recent history and sends it as a file.\n\n"
        "<b>/download</b>\n"
        "Sends the current SD log file to Telegram for review or backup.\n\n"
        "<b>Status Header</b>\n"
        "The dashboard header shows LoRa quality, RSSI, SNR, connection uptime, and device uptime. If packets stop arriving, the link is marked disconnected.\n\n"
        "<b>Group Chats</b>\n"
        "Commands with bot mentions like <code>/dashboard@YourBotName</code> are accepted automatically. Use <code>/help</code> for the workflow guide.");
}

String GreenhouseTelegramBot::buildConfigText() const {
    String text = "<b>Config Parameters</b>\n";
    text += "Use <code>/config</code> to open the selector menu, tap a parameter, then reply with the new value. You can also use <code>/config &lt;parameter&gt; &lt;value&gt;</code> directly.\n\n";
    for (int i = 0; i < numSettings; i++) {
        String key = settings[i].key;
        key.toLowerCase();
        text += "<code>" + key + "</code> - ";
        text += settings[i].name + " = ";
        text += String(*(settings[i].valueRef), 1) + settings[i].unit;
        text += " (range " + String(settings[i].minValue, 1) + " to " + String(settings[i].maxValue, 1) + settings[i].unit + ")\n";
    }
    text += "\nExamples:\n";
    text += "<code>/config temp1 82.5</code>\n";
    text += "<code>/config moist 55</code>";
    return text;
}

String GreenhouseTelegramBot::buildConfigPrompt(int settingIdx) const {
    if (settingIdx < 0 || settingIdx >= numSettings) return String();

    const SettingsParameter& setting = settings[settingIdx];
    String prompt = "<b>" + setting.icon + " " + setting.name + "</b>\n";
    prompt += "Current value: <code>" + String(*(setting.valueRef), 1) + setting.unit + "</code>\n";
    prompt += "Reply with a new value between <code>" + String(setting.minValue, 1) + "</code> and <code>" + String(setting.maxValue, 1) + setting.unit + "</code>.\n";
    prompt += "Example: <code>" + String((setting.minValue + setting.maxValue) / 2.0, 1) + "</code>";
    return prompt;
}

String GreenhouseTelegramBot::buildAlertPrompt(int sensorIdx, AlertThresholdType thresholdType) const {
    if (sensorIdx < 0 || sensorIdx >= numSensors || !sensorAlerts) return String();

    String direction = (thresholdType == ALERT_THRESHOLD_LOW) ? "low" : "high";
    String prompt = "<b>" + String(sensorMetadata[sensorIdx].name) + " " + direction + " alert</b>\n";

    if (logBuffer && logBuffer->getCount() > 0) {
        float currentValue = logBuffer->get(logBuffer->getCount() - 1).*(sensorMetadata[sensorIdx].valueField);
        prompt += "Current reading: <code>" + formatSensorReading(sensorIdx, currentValue) + "</code>\n";
    }

    if (thresholdType == ALERT_THRESHOLD_LOW && sensorAlerts[sensorIdx].lowEnabled) {
        prompt += "Current threshold: <code>" + formatSensorReading(sensorIdx, sensorAlerts[sensorIdx].lowThreshold) + "</code>\n";
    }
    if (thresholdType == ALERT_THRESHOLD_HIGH && sensorAlerts[sensorIdx].highEnabled) {
        prompt += "Current threshold: <code>" + formatSensorReading(sensorIdx, sensorAlerts[sensorIdx].highThreshold) + "</code>\n";
    }

    prompt += "Reply with the new threshold value, or send /cancel.";
    return prompt;
}

String GreenhouseTelegramBot::buildModeStatusText(BotOperatingMode mode) const {
    return mode == MODE_DASHBOARD ? String("dashboard") : String("chat");
}

String GreenhouseTelegramBot::buildBootText(BotOperatingMode mode) const {
    String text = "<b>Greenhouse bot connected.</b>\n";
    text += "Mode: <code>" + buildModeStatusText(mode) + "</code>\n";
    text += mode == MODE_DASHBOARD
        ? "Use /chat to switch this chat back to command mode.\n"
        : "Use /dashboard to switch this chat into dashboard mode.\n";
    text += "Try /help for the workflow guide.";
    return text;
}

String GreenhouseTelegramBot::buildControlsText() const {
    String text = "<b>Manual Controls</b>\n";
    text += "Use <code>/control</code> to open the interactive menu, or <code>/control &lt;device&gt; &lt;state&gt;</code> to send a command directly.\n\n";
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

String GreenhouseTelegramBot::buildSensorAlertSummary(int sensorIdx) const {
    if (sensorIdx < 0 || sensorIdx >= numSensors || !sensorAlerts) return String();

    String summary = sensorMetadata[sensorIdx].name;
    summary += " (L:";
    summary += sensorAlerts[sensorIdx].lowEnabled ? formatSensorReading(sensorIdx, sensorAlerts[sensorIdx].lowThreshold) : String("off");
    summary += " H:";
    summary += sensorAlerts[sensorIdx].highEnabled ? formatSensorReading(sensorIdx, sensorAlerts[sensorIdx].highThreshold) : String("off");
    summary += ")";
    return summary;
}

String GreenhouseTelegramBot::buildEventAlertSummary(int eventIdx) const {
    if (eventIdx < 0 || eventIdx >= numEvents || !eventAlerts) return String();
    return String(eventMetadata[eventIdx].name) + " alerts: " + (eventAlerts[eventIdx].enabled ? "ON" : "OFF");
}

String GreenhouseTelegramBot::formatSensorReading(int sensorIdx, float value) const {
    if (sensorIdx < 0 || sensorIdx >= numSensors) return String(value, 1);
    return String(value, 1) + String(sensorMetadata[sensorIdx].unit);
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

String GreenhouseTelegramBot::chatIDKey(fb::ID chatID) const {
    return String(((Text)chatID).c_str());
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
        String key = normalizeToken(eventMetadata[i].key ? eventMetadata[i].key : "");
        String name = normalizeToken(eventMetadata[i].name);
        if ((key.length() && normalized == key) || normalized == name || name.indexOf(normalized) >= 0) {
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

int GreenhouseTelegramBot::findSensorIndex(const String& token) const {
    String normalized = normalizeToken(token);
    for (int i = 0; i < numSensors; i++) {
        String name = normalizeToken(sensorMetadata[i].name);
        if (normalized == name || name.indexOf(normalized) >= 0) {
            return i;
        }
    }
    return -1;
}

void GreenhouseTelegramBot::clearPendingAlertEdit() {
    pendingAlertSensorIndex = -1;
    pendingAlertThresholdType = ALERT_THRESHOLD_NONE;
    pendingAlertChatID = "";
}

bool GreenhouseTelegramBot::hasPendingAlertEdit(const String& chatID) const {
    return pendingAlertSensorIndex >= 0 && pendingAlertThresholdType != ALERT_THRESHOLD_NONE && pendingAlertChatID == chatID;
}

void GreenhouseTelegramBot::notifyAlertChats(const String& text) {
    if (personalChatSession.chatID.length()) {
        sendTextToChat(personalChatSession.chatID.c_str(), text, true);
    }
    if (groupChatSession.chatID.length() && groupChatSession.chatID != personalChatSession.chatID) {
        sendTextToChat(groupChatSession.chatID.c_str(), text, true);
    }
}

ChatSessionState* GreenhouseTelegramBot::findSessionByChatID(const String& chatID) {
    if (personalChatSession.chatID.length() && personalChatSession.chatID == chatID) return &personalChatSession;
    if (groupChatSession.chatID.length() && groupChatSession.chatID == chatID) return &groupChatSession;
    return nullptr;
}

const ChatSessionState* GreenhouseTelegramBot::findSessionByChatID(const String& chatID) const {
    if (personalChatSession.chatID.length() && personalChatSession.chatID == chatID) return &personalChatSession;
    if (groupChatSession.chatID.length() && groupChatSession.chatID == chatID) return &groupChatSession;
    return nullptr;
}

BotOperatingMode GreenhouseTelegramBot::getModeForChat(const String& chatID) const {
    const ChatSessionState* session = findSessionByChatID(chatID);
    return session ? session->mode : operatingMode;
}

void GreenhouseTelegramBot::setModeForChat(const String& chatID, BotOperatingMode mode) {
    ChatSessionState* session = findSessionByChatID(chatID);
    if (!session) return;
    session->mode = mode;
    if (mode == MODE_CHAT) {
        session->dashboardLiveView = false;
        session->dashboardMsgID = 0;
    }
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
    String chatIDKey = String(chat.id().c_str());
    BotOperatingMode chatMode = getModeForChat(chatIDKey);
    bool isGroupChat = chatType == decltype(chatType)::group || chatType == decltype(chatType)::supergroup;
    bool hasPendingConfig = activeConfigIndex >= 0 && pendingConfigChatID == chatIDKey;
    bool hasPendingAlert = hasPendingAlertEdit(chatIDKey);

    if (command == "/dashboard" || command == "/display") {
        setModeForChat(chatIDKey, MODE_DASHBOARD);
        sendDashboardMainMenu(chatID);
        if (chatMode != MODE_DASHBOARD) {
            bot.sendMessage(fb::Message("Switched this chat to dashboard mode. Use /chat to switch back.", chatID), false);
        }
        return;
    }

    if (command == "/chat") {
        setModeForChat(chatIDKey, MODE_CHAT);
        fb::Message msg("Switched this chat to chat mode. Use /dashboard to switch back, or /help for the command guide.", chatID);
        bot.sendMessage(msg, false);
        return;
    }

    if (command == "/cancel" && (hasPendingConfig || hasPendingAlert)) {
        activeConfigIndex = -1;
        pendingConfigChatID = "";
        clearPendingAlertEdit();
        fb::Message msg("Pending update cancelled.", chatID);
        bot.sendMessage(msg, false);
        return;
    }

    if (isGroupChat && !command.startsWith("/") && !(hasPendingConfig || hasPendingAlert)) {
        return;
    }

    if (hasPendingAlert) {
        if (command.startsWith("/")) {
            clearPendingAlertEdit();
        } else {
            char* endPtr = nullptr;
            float value = strtof(text.c_str(), &endPtr);
            if (endPtr == text.c_str() || (endPtr && *endPtr != '\0')) {
                fb::Message msg("Invalid value. Please reply with a number only, or send /cancel.", chatID);
                bot.sendMessage(msg, false);
                return;
            }

            if (!sensorAlerts || pendingAlertSensorIndex < 0 || pendingAlertSensorIndex >= numSensors) {
                clearPendingAlertEdit();
                bot.sendMessage(fb::Message("Alert configuration is unavailable right now.", chatID), false);
                return;
            }

            SensorAlertConfig& alert = sensorAlerts[pendingAlertSensorIndex];
            bool isLow = pendingAlertThresholdType == ALERT_THRESHOLD_LOW;
            if (isLow && alert.highEnabled && value >= alert.highThreshold) {
                fb::Message msg("Low alert must be below the current high alert threshold. Reply with a smaller number, or send /cancel.", chatID);
                bot.sendMessage(msg, false);
                return;
            }
            if (!isLow && alert.lowEnabled && value <= alert.lowThreshold) {
                fb::Message msg("High alert must be above the current low alert threshold. Reply with a larger number, or send /cancel.", chatID);
                bot.sendMessage(msg, false);
                return;
            }

            if (isLow) {
                alert.lowEnabled = true;
                alert.lowThreshold = value;
                alert.lowActive = false;
                if (sensorAlertChangedCb) sensorAlertChangedCb(pendingAlertSensorIndex, ALERT_THRESHOLD_LOW, true, value);
            } else {
                alert.highEnabled = true;
                alert.highThreshold = value;
                alert.highActive = false;
                if (sensorAlertChangedCb) sensorAlertChangedCb(pendingAlertSensorIndex, ALERT_THRESHOLD_HIGH, true, value);
            }

            String msgText = isLow ? "✅ Low alert set for " : "✅ High alert set for ";
            msgText += sensorMetadata[pendingAlertSensorIndex].name;
            msgText += " at <code>" + formatSensorReading(pendingAlertSensorIndex, value) + "</code>";

            if (logBuffer && logBuffer->getCount() > 0) {
                float currentValue = logBuffer->get(logBuffer->getCount() - 1).*(sensorMetadata[pendingAlertSensorIndex].valueField);
                bool triggered = isLow ? (currentValue <= value) : (currentValue >= value);
                if (isLow) {
                    alert.lowActive = triggered;
                } else {
                    alert.highActive = triggered;
                }
                if (triggered) {
                    msgText += "\nCurrent reading is already at <code>" + formatSensorReading(pendingAlertSensorIndex, currentValue) + "</code>.";
                }
            }

            fb::Message msg(msgText, chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);
            clearPendingAlertEdit();
            return;
        }
    }

    if (chatMode == MODE_DASHBOARD) {
        if (command == "/start") {
            sendDashboardMainMenu(chatID);
        } else if (text == "Live Dashboard") {
            sendDashboardMainMenu(chatID);
        } else if (command == "/control") {
            sendControlsMenu(chatID);
        } else if (command == "/config") {
            sendConfigMainMenu(chatID);
        } else if (command == "/alert") {
            sendAlertsMenu(chatID);
        } else if (command == "/help" || command == "/guide") {
            fb::Message msg(buildGuideText(), chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);
        }
        return;
    }

    if (hasPendingConfig) {
        if (command.startsWith("/")) {
            activeConfigIndex = -1;
            pendingConfigChatID = "";
        }

        if (!command.startsWith("/")) {
            char* endPtr = nullptr;
            float value = strtof(text.c_str(), &endPtr);
            if (endPtr == text.c_str() || (endPtr && *endPtr != '\0')) {
                fb::Message msg("Invalid value. Please reply with a number only, or send /cancel.", chatID);
                bot.sendMessage(msg, false);
                return;
            }

            const SettingsParameter& setting = settings[activeConfigIndex];
            if (value < setting.minValue || value > setting.maxValue) {
                fb::Message msg("Value out of range for " + setting.name + ". Reply with a value between " + String(setting.minValue, 1) + " and " + String(setting.maxValue, 1) + setting.unit + ", or send /cancel.", chatID);
                bot.sendMessage(msg, false);
                return;
            }

            *(setting.valueRef) = value;
            if (settingChangedCb) settingChangedCb(setting.key, value);

            fb::Message msg("✅ " + setting.name + " updated to " + String(value, 1) + setting.unit, chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);

            activeConfigIndex = -1;
            pendingConfigChatID = "";
            return;
        }
    }

    if (command == "/start") {
        fb::Message msg("Welcome to Greenhouse Control. This chat is in <code>chat</code> mode. Use /dashboard to switch modes, or /help for the command guide.", chatID);
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
    if (command == "/alert") {
        sendAlertsMenu(chatID);
        return;
    }
    if (command == "/status") {
        sendUnicodeGraph(chatID);
        return;
    }
    if (command == "/snapshot") {
        sendSnapshot(chatID);
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
    if (command == "/control") {
        if (!args.length()) {
            sendControlsMenu(chatID);
            return;
        }

        String controlArgs = args;
        controlArgs.trim();

        int splitIdx = controlArgs.lastIndexOf(' ');
        if (splitIdx <= 0 || splitIdx >= (int)controlArgs.length() - 1) {
            fb::Message msg(buildControlsText(), chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);
            return;
        }

        String deviceToken = controlArgs.substring(0, splitIdx);
        String stateToken = controlArgs.substring(splitIdx + 1);
        deviceToken.trim();
        stateToken.trim();
        stateToken = normalizeToken(stateToken);

        int eventIdx = findEventIndex(deviceToken);
        if (eventIdx < 0 || !eventMetadata[eventIdx].key) {
            bot.sendMessage(fb::Message("Unknown control. Use /control to open the control menu.", chatID), false);
            return;
        }

        if (!controlCommandCb) {
            bot.sendMessage(fb::Message("Controls are unavailable right now. Please try again in a moment.", chatID), false);
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
            bot.sendMessage(fb::Message("Unknown control state. Use on/off, 1/0, or open /control for the interactive menu.", chatID), false);
            return;
        }

        controlCommandCb(eventMetadata[eventIdx].key, state);
        bot.sendMessage(fb::Message(String("Command sent: ") + eventMetadata[eventIdx].name + " -> " + (state ? eventMetadata[eventIdx].onStr : eventMetadata[eventIdx].offStr), chatID), false);
        return;
    }
    if (command == "/config") {
        if (!args.length()) {
            sendConfigMainMenu(chatID);
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

        if (value < settings[settingIdx].minValue || value > settings[settingIdx].maxValue) {
            bot.sendMessage(fb::Message("Value out of range. Use /config to see valid ranges.", chatID), false);
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
        bot.sendMessage(fb::Message("Unknown command. Use /help to see the available commands.", chatID), false);
    }
}

void GreenhouseTelegramBot::handleQuery(fb::Update& u) {
    fb::ID chatID = u.query().message().chat().id();
    String chatKey = chatIDKey(chatID);
    BotOperatingMode chatMode = getModeForChat(chatKey);
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
    if (qData == "menu_alerts") {
        bot.answerCallbackQuery(u.query().id());
        sendAlertsMenu(chatID, msgID);
        return;
    }
    if (qData == "menu_display") {
        bot.answerCallbackQuery(u.query().id());
        if (ChatSessionState* session = findSessionByChatID(chatKey)) {
            session->dashboardMsgID = sendUnicodeGraph(chatID, msgID);
            session->dashboardLiveView = true;
        } else {
            sendUnicodeGraph(chatID, msgID);
        }
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
        return;
    }
    if (qData == "alert_root") {
        bot.answerCallbackQuery(u.query().id());
        sendAlertsMenu(chatID, msgID);
        return;
    }
    if (qData == "alert_sensors") {
        bot.answerCallbackQuery(u.query().id());
        sendSensorAlertsMenu(chatID, msgID);
        return;
    }
    if (qData == "alert_events") {
        bot.answerCallbackQuery(u.query().id());
        sendEventAlertsMenu(chatID, msgID);
        return;
    }
    if (qData == "dl_svg_day" || qData == "dl_svg_week") {
        bot.answerCallbackQuery(u.query().id(), "SVG sent");
        sendSvgGraph(chatID);
        return;
    }

    if (qData.startsWith("alert_sensor_")) {
        bot.answerCallbackQuery(u.query().id());
        int idx = qData.substring(String("alert_sensor_").length()).toInt();
        if (idx >= 0 && idx < numSensors) {
            sendSensorAlertDetailMenu(chatID, msgID, idx);
        }
        return;
    }
    if (qData.startsWith("alert_setlow_")) {
        bot.answerCallbackQuery(u.query().id(), "Reply with the low threshold");
        pendingAlertSensorIndex = qData.substring(String("alert_setlow_").length()).toInt();
        pendingAlertThresholdType = ALERT_THRESHOLD_LOW;
        pendingAlertChatID = chatKey;
        fb::Message msg(buildAlertPrompt(pendingAlertSensorIndex, pendingAlertThresholdType), chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.sendMessage(msg, false);
        return;
    }
    if (qData.startsWith("alert_sethigh_")) {
        bot.answerCallbackQuery(u.query().id(), "Reply with the high threshold");
        pendingAlertSensorIndex = qData.substring(String("alert_sethigh_").length()).toInt();
        pendingAlertThresholdType = ALERT_THRESHOLD_HIGH;
        pendingAlertChatID = chatKey;
        fb::Message msg(buildAlertPrompt(pendingAlertSensorIndex, pendingAlertThresholdType), chatID);
        msg.mode = fb::Message::Mode::HTML;
        bot.sendMessage(msg, false);
        return;
    }
    if (qData.startsWith("alert_offlow_")) {
        bot.answerCallbackQuery(u.query().id(), "Low alert disabled");
        int idx = qData.substring(String("alert_offlow_").length()).toInt();
        if (sensorAlerts && idx >= 0 && idx < numSensors) {
            sensorAlerts[idx].lowEnabled = false;
            sensorAlerts[idx].lowActive = false;
            if (sensorAlertChangedCb) sensorAlertChangedCb(idx, ALERT_THRESHOLD_LOW, false, sensorAlerts[idx].lowThreshold);
            sendSensorAlertDetailMenu(chatID, msgID, idx);
        }
        return;
    }
    if (qData.startsWith("alert_offhigh_")) {
        bot.answerCallbackQuery(u.query().id(), "High alert disabled");
        int idx = qData.substring(String("alert_offhigh_").length()).toInt();
        if (sensorAlerts && idx >= 0 && idx < numSensors) {
            sensorAlerts[idx].highEnabled = false;
            sensorAlerts[idx].highActive = false;
            if (sensorAlertChangedCb) sensorAlertChangedCb(idx, ALERT_THRESHOLD_HIGH, false, sensorAlerts[idx].highThreshold);
            sendSensorAlertDetailMenu(chatID, msgID, idx);
        }
        return;
    }
    if (qData.startsWith("alert_event_on_")) {
        bot.answerCallbackQuery(u.query().id(), "Event alert enabled");
        int idx = qData.substring(String("alert_event_on_").length()).toInt();
        if (eventAlerts && idx >= 0 && idx < numEvents) {
            eventAlerts[idx].enabled = true;
            if (eventAlertChangedCb) eventAlertChangedCb(idx, true);
            sendEventAlertsMenu(chatID, msgID);
        }
        return;
    }
    if (qData.startsWith("alert_event_off_")) {
        bot.answerCallbackQuery(u.query().id(), "Event alert disabled");
        int idx = qData.substring(String("alert_event_off_").length()).toInt();
        if (eventAlerts && idx >= 0 && idx < numEvents) {
            eventAlerts[idx].enabled = false;
            if (eventAlertChangedCb) eventAlertChangedCb(idx, false);
            sendEventAlertsMenu(chatID, msgID);
        }
        return;
    }
    
    // Setting selections
    if (qData.startsWith("csel_")) {
        bot.answerCallbackQuery(u.query().id());
        int idx = qData.substring(5).toInt();
        if (idx >= 0 && idx < numSettings) {
            if (chatMode == MODE_CHAT) {
                activeConfigIndex = idx;
                pendingConfigChatID = String(u.query().message().chat().id().c_str());
                fb::Message msg(buildConfigPrompt(idx) + "\n\nSend /cancel to stop.", chatID);
                msg.mode = fb::Message::Mode::HTML;
                bot.sendMessage(msg, false);
            } else {
                activeConfigIndex = idx;
                sendConfigEditMenu(chatID, msgID, idx);
            }
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
        if (idx >= 0 && idx < numEvents && eventMetadata[idx].key && controlCommandCb) {
            controlCommandCb(eventMetadata[idx].key, true);
            if (chatMode == MODE_CHAT) {
                fb::Message msg(String("Command sent: ") + eventMetadata[idx].name + " -> " + eventMetadata[idx].onStr, chatID);
                bot.sendMessage(msg, false);
            }
        }
        return;
    }
    if (qData.startsWith("evoff_")) {
        bot.answerCallbackQuery(u.query().id(), "Command executing");
        int idx = qData.substring(6).toInt();
        if (idx >= 0 && idx < numEvents && eventMetadata[idx].key && controlCommandCb) {
            controlCommandCb(eventMetadata[idx].key, false);
            if (chatMode == MODE_CHAT) {
                fb::Message msg(String("Command sent: ") + eventMetadata[idx].name + " -> " + eventMetadata[idx].offStr, chatID);
                bot.sendMessage(msg, false);
            }
        }
        return;
    }
}

void GreenhouseTelegramBot::sendDashboardMainMenu(fb::ID chatID, uint32_t editMsgID) {
    String chatKey = chatIDKey(chatID);
    if (ChatSessionState* session = findSessionByChatID(chatKey)) {
        session->dashboardMsgID = sendUnicodeGraph(chatID, editMsgID);
        session->dashboardLiveView = true;
    } else {
        sendUnicodeGraph(chatID, editMsgID);
    }
}

void GreenhouseTelegramBot::sendControlsMenu(fb::ID chatID, uint32_t editMsgID) {
    BotOperatingMode chatMode = getModeForChat(chatIDKey(chatID));
    String txt = "<b>Manual Equipment Override</b>\n"
                 "Tap a control below to toggle on/off";
    if (chatMode == MODE_CHAT) {
        txt += "\nShortcut: /control &lt;device&gt; &lt;state&gt;";
        txt += "\nExample: <code>/control exhaust fan on</code>";
    }
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }
    
    String lbls = "";
    String cbs = "";

    for (int i = 0; i < numEvents; i++) {
        if (!eventMetadata[i].key || !eventMetadata[i].key[0]) continue;

        if (lbls.length() > 0) {
            lbls += "\n";
            cbs += ";";
        }

        lbls += String(eventMetadata[i].name) + " " + String(eventMetadata[i].onStr) + ";" +
                String(eventMetadata[i].name) + " " + String(eventMetadata[i].offStr);
        cbs += "evon_" + String(i) + ";evoff_" + String(i);
    }

    if (lbls.length() == 0) {
        txt += "\n\nNo control targets are configured.";
        if (editMsgID == 0) {
            fb::Message msg(txt, chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.sendMessage(msg, false);
        } else {
            fb::TextEdit msg(txt, editMsgID, chatID);
            msg.mode = fb::Message::Mode::HTML;
            bot.editText(msg, false);
        }
        return;
    }

    if (chatMode == MODE_DASHBOARD) {
        lbls += "\n🔙 Back";
        cbs += ";dash_back";
    }
    
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

void GreenhouseTelegramBot::sendSvgMenu(fb::ID chatID, uint32_t editMsgID) {
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }
    String txt = "<b>Generate High Def SVG</b>";
    fb::InlineMenu menu("Download Day\nDownload Week\n🔙 Back", "dl_svg_day;dl_svg_week;dash_back");
    fb::TextEdit msg(txt, editMsgID, chatID);
    msg.mode = fb::Message::Mode::HTML;
    msg.setInlineMenu(menu);
    bot.editText(msg, false);
}


void GreenhouseTelegramBot::sendConfigMainMenu(fb::ID chatID, uint32_t editMsgID) {
    BotOperatingMode chatMode = getModeForChat(chatIDKey(chatID));
    String txt = (chatMode == MODE_CHAT)
        ? String("<b>⚙️ Set Point Selection</b>\nTap a parameter, then reply with the new value when prompted.\nShortcut: /config &lt;parameter&gt; &lt;value&gt;\nExample: <code>/config temp1 82.5</code>")
        : String("<b>⚙️ Interactive Configuration</b>\nSelect a parameter to adjust:");
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }
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
    if (chatMode == MODE_DASHBOARD) {
        lbls += "\n🔙 Back";
        cbs += ";dash_back";
    }
    
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
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }

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

void GreenhouseTelegramBot::sendAlertsMenu(fb::ID chatID, uint32_t editMsgID) {
    BotOperatingMode chatMode = getModeForChat(chatIDKey(chatID));
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }

    String txt = "<b>Alert Setup</b>\nChoose what kind of alerts you want to manage.";
    String lbls = "Sensor Alerts;Event Alerts";
    String cbs = "alert_sensors;alert_events";
    if (chatMode == MODE_DASHBOARD) {
        lbls += "\n🔙 Back";
        cbs += ";dash_back";
    }

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

void GreenhouseTelegramBot::sendSensorAlertsMenu(fb::ID chatID, uint32_t editMsgID) {
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }

    String txt = "<b>Sensor Alerts</b>\nSelect a sensor, then choose whether to manage its low or high threshold.";
    String lbls;
    String cbs;
    for (int i = 0; i < numSensors; i++) {
        if (i > 0) {
            lbls += "\n";
            cbs += ";";
        }
        lbls += buildSensorAlertSummary(i);
        cbs += "alert_sensor_" + String(i);
    }
    lbls += "\n⬅ Alerts";
    cbs += ";alert_root";

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

void GreenhouseTelegramBot::sendSensorAlertDetailMenu(fb::ID chatID, uint32_t editMsgID, int sensorIndex) {
    if (sensorIndex < 0 || sensorIndex >= numSensors || !sensorAlerts) return;
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }

    String txt = "<b>" + String(sensorMetadata[sensorIndex].name) + " Alerts</b>\n";
    if (logBuffer && logBuffer->getCount() > 0) {
        float currentValue = logBuffer->get(logBuffer->getCount() - 1).*(sensorMetadata[sensorIndex].valueField);
        txt += "Current reading: <code>" + formatSensorReading(sensorIndex, currentValue) + "</code>\n";
    }
    txt += "Low alert: <code>";
    txt += sensorAlerts[sensorIndex].lowEnabled ? formatSensorReading(sensorIndex, sensorAlerts[sensorIndex].lowThreshold) : String("off");
    txt += "</code>\nHigh alert: <code>";
    txt += sensorAlerts[sensorIndex].highEnabled ? formatSensorReading(sensorIndex, sensorAlerts[sensorIndex].highThreshold) : String("off");
    txt += "</code>\n\nChoose a threshold to set or disable.";

    String lbls = "Set Low;Disable Low\nSet High;Disable High\n⬅ Sensors";
    String cbs = "alert_setlow_" + String(sensorIndex) + ";alert_offlow_" + String(sensorIndex) + ";" +
                 "alert_sethigh_" + String(sensorIndex) + ";alert_offhigh_" + String(sensorIndex) + ";alert_sensors";

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

void GreenhouseTelegramBot::sendEventAlertsMenu(fb::ID chatID, uint32_t editMsgID) {
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }

    String txt = "<b>Event Alerts</b>\nEquipment alerts are enabled by default. Turn them on or off below.";
    for (int i = 0; i < numEvents; i++) {
        txt += "\n" + buildEventAlertSummary(i);
    }

    String lbls;
    String cbs;
    for (int i = 0; i < numEvents; i++) {
        if (i > 0) {
            lbls += "\n";
            cbs += ";";
        }
        lbls += String(eventMetadata[i].name) + " On;" + String(eventMetadata[i].name) + " Off";
        cbs += "alert_event_on_" + String(i) + ";alert_event_off_" + String(i);
    }
    lbls += "\n⬅ Alerts";
    cbs += ";alert_root";

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

void GreenhouseTelegramBot::sendGuideMenu(fb::ID chatID, uint32_t editMsgID) {
    if (ChatSessionState* session = findSessionByChatID(chatIDKey(chatID))) {
        session->dashboardLiveView = false;
    }

    String txt =
        "<b>User Guide</b>\n"
        "<i>Quick reference for the live dashboard workflow.</i>\n\n"
        "<b>Chat Mode vs Dashboard Mode</b>\n"
        "Use <code>/chat</code> for command-and-reply control, or <code>/dashboard</code> to keep this chat on the live inline dashboard. Each chat tracks its own mode independently.\n\n"
        "<b>Display</b>\n"
        "Shows live link health, actuator history, and sensor sparklines. Use this as the always-on status view for a dashboard chat.\n\n"
        "<b>Controls</b>\n"
        "Sends manual LoRa commands for fan, sides, and irrigation. Use for testing or temporary overrides. The same actions are available in chat mode with <code>/control</code>.\n\n"
        "<b>Alerts</b>\n"
        "Use <code>/alert</code> or the dashboard Alerts button to configure sensor thresholds and event notifications. Equipment alerts for fan, roller sides, and irrigation start enabled by default.\n\n"
        "<b>Set Points</b>\n"
        "Adjusts target temperatures and moisture thresholds. In the dashboard, use the +/- buttons. In chat mode, use <code>/config</code>, tap a parameter, then reply with a value.\n\n"
        "<b>Snapshot and Status</b>\n"
        "Use <code>/snapshot</code> for a compact reading summary or <code>/status</code> for the full Unicode text dashboard without changing modes.\n\n"
        "<b>Download Logs</b>\n"
        "Sends the current SD log file to Telegram for offline review or backup.\n\n"
        "<b>SVG Plot</b>\n"
        "Generates a high-resolution plot from recent history and sends it as a file. Best for detailed trend review.\n\n"
        "<b>Status Header</b>\n"
        "The top line shows LoRa quality, RSSI, SNR, connection uptime, and device uptime. If no packets arrive for a while, the link is marked disconnected.\n\n"
        "<b>Typical Workflow</b>\n"
        "Keep one chat on the dashboard for passive monitoring, use another chat in command mode for controls and set points, and export logs or SVG when you need history.";

    fb::InlineMenu menu("🔙 Back", "dash_back");

    fb::TextEdit msg(txt, editMsgID, chatID);
    msg.mode = fb::Message::Mode::HTML;
    msg.setInlineMenu(menu);
    bot.editText(msg, false);
}

void GreenhouseTelegramBot::sendSnapshot(fb::ID chatID) {
    if (!logBuffer || numSensors == 0 || logBuffer->getCount() == 0) {
        fb::Message msg("No live data yet. Waiting for sensor updates over LoRa.", chatID);
        bot.sendMessage(msg, false);
        return;
    }

    size_t totalCount = logBuffer->getCount();
    LogEntry current = logBuffer->get(totalCount - 1);
    LogEntry previous = (totalCount > 1) ? logBuffer->get(totalCount - 2) : current;

    String msgText = "<b>Greenhouse Snapshot</b>\n";
    msgText += "<code>Updated " + formatTime(current.timestamp) + "</code>\n\n";
    msgText += "<u><b>Readings</b></u>\n";

    for (int sensorIndex = 0; sensorIndex < numSensors; sensorIndex++) {
        float currentValue = current.*(sensorMetadata[sensorIndex].valueField);
        float previousValue = previous.*(sensorMetadata[sensorIndex].valueField);
        String trendArrow = "→";
        if (currentValue > previousValue) trendArrow = "↗";
        else if (currentValue < previousValue) trendArrow = "↘";

        msgText += trendArrow + String(" ") + sensorMetadata[sensorIndex].name + ": ";
        msgText += String(currentValue, 1) + " " + sensorMetadata[sensorIndex].unit + "\n";
    }

    if (numEvents > 0) {
        msgText += "\n<u><b>Actuators</b></u>\n";
        for (int eventIndex = 0; eventIndex < numEvents; eventIndex++) {
            bool state = current.*(eventMetadata[eventIndex].stateField);
            msgText += String(state ? eventMetadata[eventIndex].emojiOn : eventMetadata[eventIndex].emojiOff);
            msgText += " " + String(eventMetadata[eventIndex].name) + ": ";
            msgText += state ? eventMetadata[eventIndex].onStr : eventMetadata[eventIndex].offStr;
            msgText += "\n";
        }
    }

    fb::Message msg(msgText, chatID);
    msg.mode = fb::Message::Mode::HTML;
    bot.sendMessage(msg, false);
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

    fb::InlineMenu menu("Controls;Set Points;Alerts\n"
                        "Display;Download;Graph\n"
                        "Guide", 
                        "menu_controls;menu_setpoints;menu_alerts;"
                        "menu_display;dl_logs;menu_svg;"
                        "menu_guide");

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
    int rawWidth = (count * pointSpacing) + marginX + 20;
    int width = (rawWidth < 500) ? 500 : rawWidth; // Minimum width so legend and labels never overlap
    int minLegendColWidth = 180;
    int legendCols = (width - marginX - 20) / minLegendColWidth;
    if (legendCols > numSensors) legendCols = numSensors;
    if (legendCols < 1) legendCols = 1;
    int legendRows = (numSensors + legendCols - 1) / legendCols;
    int legendRowHeight = 18;
    int marginYTop = 44 + (legendRows * legendRowHeight); // Extra top room for wrapped legend rows
    int graphHeight = 200; // Fixed plot area height so data always fills the frame
    int height = marginYTop + graphHeight + marginYBottom;
    int graphWidth = width - marginX - 20;

    auto normalizedPlotColor = [](const char* rawColor) {
        String color = rawColor ? String(rawColor) : String("#7ec8ff");
        String lowered = color;
        lowered.toLowerCase();
        if (lowered == "#ffff00" || lowered == "#ff0" || lowered == "yellow") {
            return String("#ffd95a");
        }
        return color;
    };
    
    // Start SVG document with responsive viewBox
    String svg = "<svg viewBox=\"0 0 " + String(width) + " " + String(height) + "\" xmlns=\"http://www.w3.org/2000/svg\" style=\"background-color:#2f343a; font-family:sans-serif;\">\n";
    svg += "  <style>\n";
    svg += "    .axis { font-size: 10px; fill: #d2d8dd; }\n";
    svg += "    .title { font-size: 14px; font-weight: bold; fill: #f2f5f7; }\n";
    svg += "    .legend { font-size: 10px; font-weight: bold; fill: #eef2f5; }\n";
    svg += "    .val { font-size: 8px; font-weight: bold; }\n";
    svg += "  </style>\n";
    
    // Background and Title
    String title = (numSensors == 1) ? String(sensorMetadata[0].name) : "Combined Sensor";
    svg += "  <text x=\"" + String(width / 2) + "\" y=\"24\" class=\"title\" text-anchor=\"middle\">" + title + " History</text>\n";

    // Horizontal divider lines to improve readability on dense charts.
    int horizontalDividers = 4;
    for (int g = 1; g <= horizontalDividers; g++) {
        int yGrid = marginYTop + ((graphHeight * g) / (horizontalDividers + 1));
        float gridVal = max_val - ((max_val - min_val) * g) / (horizontalDividers + 1);
        svg += "  <line x1=\"" + String(marginX) + "\" y1=\"" + String(yGrid) + "\" x2=\"" + String(width - 20) + "\" y2=\"" + String(yGrid) + "\" stroke=\"#505861\" stroke-width=\"1\" stroke-dasharray=\"5 5\" />\n";
        svg += "  <text x=\"" + String(marginX - 6) + "\" y=\"" + String(yGrid + 3) + "\" style=\"font-size:8px;fill:#8a929a;\" text-anchor=\"end\">" + String(gridVal, 1) + "</text>\n";
    }

    // Draw Axes
    svg += "  <line x1=\"" + String(marginX) + "\" y1=\"" + String(marginYTop) + "\" x2=\"" + String(marginX) + "\" y2=\"" + String(height - marginYBottom) + "\" stroke=\"#aeb7bf\" stroke-width=\"1.5\" />\n"; // Y Axis
    svg += "  <line x1=\"" + String(marginX) + "\" y1=\"" + String(height - marginYBottom) + "\" x2=\"" + String(width - 20) + "\" y2=\"" + String(height - marginYBottom) + "\" stroke=\"#aeb7bf\" stroke-width=\"1.5\" />\n"; // X Axis

    // Y axis tick labels at top/mid/bottom improve reading across multiple lines.
    for (int t = 0; t <= 2; t++) {
        float ratio = t / 2.0f;
        float tickVal = max_val - ((max_val - min_val) * ratio);
        int yTick = marginYTop + int(graphHeight * ratio);
        svg += "  <text x=\"" + String(marginX - 6) + "\" y=\"" + String(yTick + 4) + "\" class=\"axis\" text-anchor=\"end\">" + String(tickVal, 1) + "</text>\n";
    }
    
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
            String color = normalizedPlotColor(eventMetadata[e].color);
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
        String color = normalizedPlotColor(sensorMetadata[h].color);
        
        // Draw wrapped legend entries to avoid overlapping labels.
        int legendCol = h % legendCols;
        int legendRow = h / legendCols;
        int legendWidth = graphWidth / legendCols;
        int legendX = marginX + (legendCol * legendWidth) + 8;
        int legendY = 44 + (legendRow * legendRowHeight);
        svg += "  <line x1=\"" + String(legendX) + "\" y1=\"" + String(legendY - 4) + "\" x2=\"" + String(legendX + 12) + "\" y2=\"" + String(legendY - 4) + "\" stroke=\"" + color + "\" stroke-width=\"3\" />\n";
        svg += "  <text x=\"" + String(legendX + 16) + "\" y=\"" + String(legendY) + "\" class=\"legend\" fill=\"" + color + "\">" + String(sensorMetadata[h].name) + " (" + String(sensorMetadata[h].unit) + ")</text>\n";

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
                // Stack value labels above the dot, fanning upward per sensor
                int yOffset = -4 - (h * 9);
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
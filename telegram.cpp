// telegram.cpp 🤖💬 Модуль Telegram бота на базе FastBot2
// 🌱💧 Система капельного полива с 8 каналами датчиков влажности
#include "telegram.h"
#include "objects.h"
#include "hashtable.h"
#include <EEPROM.h>
#include <CharPlot.h>
#include "valves.h"

// ============================================================
// 🔄 Флаг необходимости сохранения конфигурации на SD-карту
// ============================================================
bool needUpdate = false;

// 🔄 Проверить и сбросить флаг необходимости обновления конфигурации
bool telegram_needUpdate() {
  bool nu = needUpdate;
  needUpdate = false;
  return nu;
}

// ============================================================
// 📨 Отправка сообщения с повторными попытками при ошибке WiFi
// ============================================================
// 💬 Отправить текстовое сообщение пользователю с 3 попытками.
//    При ошибке связи с Telegram (result.isError()) — переподключает WiFi
void sendReconnectMessage(String text, String id, bool kbRem = false) {
  for (int i = 0; i < 3; i++) {
    // 📝 Создаём объект сообщения FastBot2
    fb::Message msg;
    msg.setModeHTML();
    msg.text = text;        // 💬 Текст сообщения
    msg.chatID = id;        // 👤 ID чата получателя


    if (kbRem)
    {
      msg.removeKeyboard();
    }

    // 📤 Отправляем сообщение (wait=true — синхронная отправка)
    fb::Result result = bot.sendMessage(msg, true);

    // ✅ Проверяем результат: если ошибка — пытаемся восстановить WiFi
    if (result.isError()) {
      WiFi.disconnect();
      delay(10);
      Serial.println("📡 Status wi-fi is broken");
      WiFi.reconnect();
      int ind = 0;
      while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
        ind++;
        if (ind > 60) {
          break;
        }
      }
      dropped = true;  // 📡 Устанавливаем флаг потери соединения
    } else {
      // ✅ Успешно отправлено — выходим из цикла попыток
      break;
    }
  }
}

// ============================================================
// 🗑️ Рекурсивное удаление папки со всем содержимым на SD-карте
// ============================================================
void rm(File dir, String tempPath) {
  while (true) {
    File entry = dir.openNextFile();
    String localPath;

    Serial.println("");
    if (entry) {
      if (entry.isDirectory()) {
        localPath = tempPath + entry.name();
        rm(entry, localPath + "/");

        if (SD.rmdir(localPath)) {
          Serial.print("🗑️ Deleted folder ");
          Serial.println(localPath);
        } else {
          Serial.print("❌ Unable to delete folder ");
          Serial.println(localPath);
        }
      } else {
        localPath = tempPath + entry.name();

        if (SD.remove(localPath)) {
          Serial.print("🗑️ Deleted ");
          Serial.println(localPath);
        } else {
          Serial.print("❌ Failed to delete ");
          Serial.println(localPath);
        }
      }
    } else {
      // 🔚 Выход из рекурсии — больше нет файлов
      break;
    }
  }
}

// 🔮 Предварительное объявление обработчика сообщений (FastBot2 callback)
void newMsg(fb::Update& u);
void loadUsers();

// ============================================================
// 🔢 Проверка строки на числовое значение (целое или с точкой)
// ============================================================
bool isNumeric(String str) {
  unsigned int stringLength = str.length();

  if (stringLength == 0) {
    return false;
  }

  boolean seenDecimal = false;

  for (unsigned int i = 0; i < stringLength; ++i) {
    if (isDigit(str.charAt(i))) {
      continue;
    }

    if (str.charAt(i) == '.') {
      if (seenDecimal) {
        return false;
      }
      seenDecimal = true;
      continue;
    }
    return false;
  }
  return true;
}

// ============================================================
// ✂️ Извлечение подстроки по разделителю (CSV-парсер)
// ============================================================
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// ============================================================
// 📝 Форматирование числа с ведущим нулём (для дат)
// ============================================================
String IntWith2Zero(int data) {
  String s = String(data);
  if (data < 10) { s = String("0") + s; }
  return s;
}

// ============================================================
// 📊 Построение графика за указанный период дней (несколько файлов)
// ============================================================
void fileToGrafPeriod(int period, String msgID) {
  sendReconnectMessage(F("📊 Началась генерация отчётов, ждите ..."), msgID);

  int del = 60 / period;
  uint8_t sz = period * del;

  float arr[8][sz];
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < sz; i++) {
      arr[j][i] = 0.0;
    }

  String buffer;
  int count[8];
  int value[8];

  for (int i = 0; i < 8; i++) {
    count[i] = 0;
    value[i] = 0;
  }

  int64_t ut = getDateTime().getUnix() - 60 * 60 * 24 * period;

  for (int p = 0; p < period; p++) {
    int64_t cut = ut + 60 * 60 * 24 * p;
    Datime t(cut);
    String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
    Serial.print("📁 Try get file :");
    Serial.println(fn);
    if (SD.exists(fn)) {
      File printFile = SD.open(fn, FILE_READ);

      if (!printFile) {
        Serial.print("❌ The text file cannot be opened");
        continue;
      }
      unsigned long part = printFile.size() / del;
      for (int d = 0; d < del; d++) {
        printFile.seek(part * d);
        printFile.readStringUntil('\n');
        int ind = 0;
        while (printFile.available()) {
          buffer = printFile.readStringUntil('\n');
          String data = getValue(buffer, ',', 0);
          String curr = getValue(buffer, ',', 2);
          int index = curr.toInt() - 1;
          curr = getValue(buffer, ',', 4);
          value[index] += curr.toInt();
          count[index]++;
          ind++;
          if (ind > 72) break;
        }
        for (int i = 0; i < 8; i++) {
          arr[i][p * del + d] = value[i] * 1.0 / count[i];
          value[i] = 0;
          count[i] = 0;
        }
      }
      printFile.close();
    }
  }

  // 📊 Отправляем графики в режиме MarkdownV2 для форматирования
  for (int i = 0; i < 8; i++) {
    fb::Message msg;
    msg.text = String("```\n🌱 Канал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X2>(arr[i], sz, 10) + String("\n```");
    msg.chatID = msgID;
    msg.setModeMD();  // 📝 MarkdownV2 режим для форматирования кода
    bot.sendMessage(msg);
  }
}

// ============================================================
// 📊 Построение графика за один день (один файл CSV)
// ============================================================
void fileToGraf(String fn, String msgID) {
  sendReconnectMessage(F("📊 Началась генерация отчётов, ждите ..."), msgID);
  File printFile = SD.open(fn, FILE_READ);

  if (!printFile) {
    Serial.print("❌ The text file cannot be opened");
    return;
  }
  uint8_t sz = 24;
  float arr[8][sz];
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < sz; i++) {
      arr[j][i] = 0.0;
    }

  String buffer;
  int ind[8];
  int count[8];
  int value[8];

  for (int i = 0; i < 8; i++) {
    ind[i] = -1;
    count[i] = 0;
    value[i] = 0;
  }
  printFile.readStringUntil('\n');

  while (printFile.available()) {
    buffer = printFile.readStringUntil('\n');
    String data = getValue(buffer, ',', 0);
    String curr = getValue(buffer, ',', 2);
    int index = curr.toInt() - 1;
    curr = getValue(buffer, ',', 4);
    int64_t ut = data.toInt();
    // 🕐 Извлекаем час из Unix-времени
    int hour = (ut % 86400) / 3600;
    if (hour > ind[index]) {
      if (ind[index] >= 0) {
        arr[index][ind[index]] = value[index] * 1.0 / count[index];
        value[index] = 0;
        count[index] = 0;
      }
      ind[index] = hour;
    }

    value[index] += curr.toInt();
    count[index]++;
  }
  for (int i = 0; i < 8; i++) {
    if (ind[i] >= 0) {
      arr[i][ind[i]] = value[i] * 1.0 / count[i];
    }
  }
  printFile.close();

  // 📊 Отправляем графики в режиме MarkdownV2
  for (int i = 0; i < 8; i++) {
    fb::Message msg;
    msg.text = String("```\n🌱 Канал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X1>(arr[i], sz, 10) + String("\n```");
    msg.chatID = msgID;
    msg.setModeMD();
    bot.sendMessage(msg);
  }
}

// ============================================================
// 📁 Рекурсивный вывод содержимого папки SD-карты в Serial
// ============================================================
void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
  Serial.printf("📁 Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("❌ Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("❌ Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  📂 DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  📄 FILE: ");
      Serial.print(file.name());
      Serial.print("  📏 SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

// ============================================================
// 🚀 Инициализация Telegram бота (FastBot2)
// ============================================================
void botInit() {
  // 🔐 Устанавливаем токен бота
  bot.setToken(BOT_TOKEN);

  // ⏭️ Пропускаем накопившиеся сообщения
  bot.skipUpdates();

  // 📎 Подключаем обработчик входящих обновлений (FastBot2 стиль)
  bot.attachUpdate(newMsg);

  bot.setPollMode(fb::Poll::Long, 60000);

  // 📥 Загружаем зарегистрированных пользователей из EEPROM
  loadUsers();
}

// ============================================================
// 👥 Классы для управления пользователями и действиями
// ============================================================

// 🎬 Класс действия пользователя (конечный автомат состояний диалога)
class Action {
public:
  String userID;  // 👤 ID пользователя Telegram
  int action;     // 🔢 Код текущего действия/состояния диалога

  Action(const String& u, int a)
    : userID(u), action(a) {}
};

// 👤 Класс пользователя Telegram
class User {
public:
  String userID;      // 👤 ID пользователя
  byte role;          // 🎭 Роль: 0=владелец, 1=админ, 2=пользователь
  bool messages = true;  // 📨 Получать ли статусные сообщения

  User(const String& u, byte r)
    : userID(u), role(r) {}
};

// 💾 Структура для сохранения пользователя в EEPROM
struct SaveUser {
  char userID[12] = "";
  byte role = 0;
};

// 🗃️ Таблицы пользователей и текущих действий (Hashtable)
Hashtable<String, User> users;
Hashtable<String, Action> actions;

// ============================================================
// 💾 Сохранение списка пользователей в EEPROM
// ============================================================
void saveUsers() {
  Serial.println("💾 begin write users");
  EEPROM.begin(4096);
  int count = users.elements();
  EEPROM.put(250, count);
  Serial.println("count users = " + String(count));
  SimpleVector<String> keys = users.keys();
  int ind = 0;
  for (const String& key : keys) {
    User* user = users.get(key);
    SaveUser usr;
    strcpy(usr.userID, user->userID.c_str());
    usr.role = user->role;
    int shift = 255 + ind * sizeof(SaveUser);
    EEPROM.put(shift, usr);
    ind++;
    Serial.println("EEPROM shift " + String(shift));
    Serial.println("write user " + String(ind) + " UserID = " + String(usr.userID) + " role = " + String(usr.role));
  }
  EEPROM.commit();
  EEPROM.end();
}

// ============================================================
// 📡 Отправка уведомления о восстановлении соединения
// ============================================================
void reConnection(unsigned long time) {
  Serial.println("📡 try send reconnect message");
  if (time > 300 * 1000) {  // ⏱️ Более 5 минут offline
    SimpleVector<String> keys = users.keys();
    for (const String& key : keys) {
      User* user = users.get(key);
      Serial.print("Send message for user: ");
      Serial.println(user->userID);
      sendReconnectMessage("🌐 Система снова в сети!", user->userID);
    }
  }
}

// ============================================================
// 💾 Отправка уведомления об отключении SD-карты
// ============================================================
void dropCDCard() {
  Serial.println("💾 try send disconnect SD CARD message");
  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    Serial.print("Send message for user: ");
    Serial.println(user->userID);
    sendReconnectMessage(F("💾 Отсутствует SD карта — система не сможет работать!"), user->userID);
  }
}

// ============================================================
// 📨 Отправка статусного сообщения всем подписанным пользователям
// ============================================================
void sendStatus(String text) {
  if (dropped) return;  // 📡 Не отправляем при отсутствии WiFi

  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    if (user->messages) {
      sendReconnectMessage(text, user->userID);
    }
  }
}

// ============================================================
// 🎬 Установить текущее действие пользователя (конечный автомат)
// ============================================================
void actionSet(String userID, int action) {
  if (actions.containsKey(userID)) {
    Action* act = actions.get(userID);
    act->action = action;
  } else {
    actions.put(userID, Action(userID, action));
  }
}

// ============================================================
// 💾 Отправка уведомления о подключении SD-карты
// ============================================================
void connectCDCard() {
  Serial.println("💾 try send SD CARD Connected message");
  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    Serial.print("Send message for user: ");
    Serial.println(user->userID);
    sendReconnectMessage(F("💾 SD карта снова активна!"), user->userID);
  }
}

// ============================================================
// 📥 Загрузка списка пользователей из EEPROM при старте
// ============================================================
void loadUsers() {
  EEPROM.begin(4096);
  int ind = 0;
  EEPROM.get(250, ind);
  Serial.println("📥 begin read users");
  Serial.println("count users = " + String(ind));
  for (int i = 0; i < ind; i++) {
    SaveUser usr;
    int shift = 255 + i * sizeof(SaveUser);
    Serial.println("EEPROM shift " + String(shift));
    EEPROM.get(shift, usr);
    users.put(String(usr.userID), User(String(usr.userID), usr.role));
    Serial.println("read user UserID = " + String(usr.userID) + " role = " + String(usr.role));
    sendReconnectMessage("🚀 Система запущена!", usr.userID);

    // 🎹 Формируем inline-меню в зависимости от роли пользователя
    if (usr.role < 2) {
      // 🎭 Администратор/владелец — полное меню
      fb::InlineKeyboard menu;
      menu.addButton("🔄 Перезагрузка", "/Restart", fb::KeyStyle::Danger).newRow()
          .addButton("👥 Пользователи", "/Users", fb::KeyStyle::Primary).newRow()
          .addButton("⚙️ Управление", "/control", fb::KeyStyle::Primary).newRow()
          .addButton("📊 Статус", "/status", fb::KeyStyle::Primary).newRow()
          .addButton("📈 Отчёты", "/reports", fb::KeyStyle::Primary).newRow()
          .addButton("🔧 Настройка", "/Configure", fb::KeyStyle::Primary);

      fb::Message msg;
      msg.text = "🚀 <b>Запуск</b>";
      msg.chatID = usr.userID;
      msg.setModeHTML();  // 📝 HTML режим
      msg.setKeyboard(&menu);
      bot.sendMessage(msg);
    } else {
      // 👤 Обычный пользователь — ограниченное меню
      fb::InlineKeyboard menu;
      menu.addButton("⚙️ Управление", "/control", fb::KeyStyle::Primary).newRow()
          .addButton("📊 Статус", "/status", fb::KeyStyle::Primary).newRow()
          .addButton("📈 Отчёты", "/reports", fb::KeyStyle::Primary);

      fb::Message msg;
      msg.text = "🚀 <b>Запуск</b>";
      msg.chatID = usr.userID;
      msg.setModeHTML();
      msg.setKeyboard(&menu);
      bot.sendMessage(msg);
    }
  }
  EEPROM.end();
}

// ============================================================
// 🔍 Массив для операции поиска датчика (временные значения АЦП)
// ============================================================
int search[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

// ============================================================
// 🤖 ГЛАВНЫЙ ОБРАБОТЧИК СООБЩЕНИЙ TELEGRAM (FastBot2)
// ============================================================
void newMsg(fb::Update& u) {
  // 📥 Получаем объект сообщения из обновления
  fb::MessageRead msg = u.message();

  // 📝 Извлекаем данные из сообщения (FastBot2 API)
  String chatID = msg.chat().id().toString();   // 💬 ID чата
  String userID = msg.from().id().toString();   // 👤 ID пользователя
  String username = msg.from().username().toString();  // 🏷️ Имя пользователя
  String text = msg.text().toString();           // 💬 Текст сообщения

  // 🔘 Проверяем, является ли это callback query (нажатие inline-кнопки)
  String data = "";
  if (u.isQuery()) {
    fb::QueryRead q = u.query();
    data = q.data().toString();
    userID = q.from().id().toString();
    chatID = q.message().chat().id().toString();
  }

  // 🖨️ Отладочный вывод в Serial
  Serial.print(userID);  // 👤 ID пользователя
  Serial.print(", ");
  Serial.print(username);  // 🏷️ Логин
  Serial.print(", ");
  Serial.print(text);      // 💬 Текст
  Serial.print(", ");
  Serial.print(data);      // 🔘 Callback data
  Serial.print(", ");
  Serial.println(u.isQuery() ? "query" : "message");

  // ============================================================
  // 🆕 ПЕРВИЧНАЯ РЕГИСТРАЦИЯ: если нет пользователей — первый вводит кодовое слово
  // ============================================================
  if (users.elements() == 0) {
    if (text == String(tstr)) {
      // ✅ Кодовое слово совпало — регистрируем как владельца (роль 0)
      fb::Message reply;
      reply.text = "👑 Привет, владелец системы!";
      reply.chatID = chatID;
      reply.reply.messageID = msg.id().toInt();  // 📎 Ответ на конкретное сообщение
      bot.sendMessage(reply);

      users.put(userID, User(userID, 0));
      saveUsers();
    } else {
      fb::Message reply;
      reply.text = "🔐 Первый запрос должен быть с ключевым словом, полученным при настройке WiFi";
      reply.chatID = chatID;
      reply.reply.messageID = msg.id().toInt();
      bot.sendMessage(reply);
    }
    return;
  }

  // ============================================================
  // 👤 ПРОВЕРКА РЕГИСТРАЦИИ ПОЛЬЗОВАТЕЛЯ
  // ============================================================
  if (users.containsKey(userID)) {
    User* check_user = users.get(userID);
    String command = "";

    // 🔘 Если это callback query — берём data, иначе текст сообщения
    if (u.isQuery()) {
      command = data;
      // ✅ Автоматически отвечаем на callback query (FastBot2 autoQuery по умолчанию)
    } else {
      command = text;
    }

    // ============================================================
    // 🎬 ОБРАБОТКА АКТИВНЫХ ДЕЙСТВИЙ (конечный автомат диалогов)
    // ============================================================
    if (actions.containsKey(userID)) {
      Action* act = actions.get(userID);

      // 🔙 Сброс действия при получении команды
      if (command.startsWith("/")) {
        act->action = 0;
      }
      // 🔧 Калибровка: этап 3 — завершение (1130–1137)
      else if (act->action >= 1130 && act->action <= 1137) {
        int ind = act->action - 1130;
        if (text == "✅ ЗАВЕРШИТЬ") {
          if (abs(hs.getHigh(ind) - hs.getLow(ind)) < 100) {
            sendReconnectMessage("❌ Ошибка калибровки датчик № " + String((ind + 1)) + " — слишком малое значение!\nОтменяем...", userID);
            hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
            command = F("/Calibrate");
          } else {
            sendReconnectMessage("✅ Калибровка завершена! Датчик № " + String((ind + 1)) + " полностью функционален!", userID);
            myConfig.chanel[ind].maxVal = hs.getHigh(ind);
            myConfig.chanel[ind].minVal = hs.getLow(ind);
            needUpdate = true;
          }
          act->action = 0;
          command = F("/Calibrate");
        } else {
          sendReconnectMessage(F("❌ Калибровка отменена!"), userID);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
          act->action = 0;
        }
      }
      // 🔧 Калибровка: этап 2 — сухое значение (1120–1127)
      else if (act->action >= 1120 && act->action <= 1127) {
        int ind = act->action - 1120;
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          int val = hs.setHigh(ind);
          Serial.print("🌵 Сухое значение: ");
          Serial.println(val);
          sendReconnectMessage("🌵 Установите датчик № " + String((ind + 1)) + " в почву и нажмите завершить!", userID);

          fb::Keyboard kb;
          kb.addButton("✅ ЗАВЕРШИТЬ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Калибровка>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          act->action = 1130 + ind;
          return;
        } else {
          sendReconnectMessage(F("❌ Калибровка отменена!"), userID, true);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
          act->action = 0;
        }
      }
      // 🔧 Калибровка: этап 1 — влажное значение (1110–1117)
      else if (act->action >= 1110 && act->action <= 1117) {
        int ind = act->action - 1110;
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          int val = hs.setLow(ind);
          Serial.print("💧 Значение с водой датчик ");
          Serial.print(ind);
          Serial.print(": ");
          Serial.println(val);
          act->action = 1120 + ind;
          sendReconnectMessage("🌵 Достаньте датчик № " + String((ind + 1)) + " из воды, протрите и нажмите далее!", userID);

          fb::Keyboard kb;
          kb.addButton("➡️ ДАЛЕЕ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Калибровка>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          return;
        } else {
          sendReconnectMessage(F("❌ Калибровка отменена!"), userID, true);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
          act->action = 0;
        }
      }
      // 🔧 Калибровка: этап 0 — старт (1100–1107)
      else if (act->action >= 1100 && act->action <= 1107) {
        if (text == "🚀 СТАРТ") {
          int ind = act->action - 1100;
          sendReconnectMessage("💧 Положите датчик № " + String((ind + 1)) + " в воду и нажмите далее!", userID);

          fb::Keyboard kb;
          kb.addButton("➡️ ДАЛЕЕ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Калибровка>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          act->action = 1110 + ind;
          return;
        } else {
          sendReconnectMessage(F("❌ Калибровка отменена!"), userID, true);
          command = F("/Calibrate");
          act->action = 0;
        }
      }
      // 🔍 Поиск датчика: этап 0 — старт (3000)
      else if (act->action == 3000) {
        if (text == "🚀 СТАРТ") {
          sendReconnectMessage("💧 Положите датчик в воду и нажмите далее!", userID);

          fb::Keyboard kb;
          kb.addButton("➡️ ДАЛЕЕ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Поиск>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          act->action = 3010;
          return;
        } else {
          sendReconnectMessage(F("❌ Поиск отменён!"), userID, true);
          command = F("/control");
          act->action = 0;
        }
      }
      // 🔍 Поиск датчика: этап 1 — влажное значение (3010)
      else if (act->action == 3010) {
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          for (int i = 0; i < 8; i++) {
            search[i] = hs.getCurrent(i);
          }
          sendReconnectMessage("🌵 Достаньте датчик из воды, протрите и нажмите далее!", userID);

          fb::Keyboard kb;
          kb.addButton("➡️ ДАЛЕЕ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Поиск>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          act->action = 3020;
          return;
        } else {
          sendReconnectMessage(F("❌ Поиск отменён!"), userID, true);
          command = F("/control");
          act->action = 0;
        }
      }
      // 🔍 Поиск датчика: этап 2 — определение (3020)
      else if (act->action == 3020) {
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          int ind = -1;
          for (int i = 0; i < 8; i++) {
            if (abs(search[i] - hs.getCurrent(i)) > 300) {
              ind = i;
              break;
            }
          }
          if (ind < 0) {
            sendReconnectMessage(F("❌ Не удалось определить датчик! Повторите операцию."), userID, true);
          } else {
            sendReconnectMessage("🔍 Предположительно ваш датчик № " + String(ind + 1) + " (" + myConfig.chanel[ind].title + ")!", userID, true);
          }
        } else {
          sendReconnectMessage(F("❌ Поиск отменён!"), userID, true);
        }
        command = F("/control");
        act->action = 0;
      }
      // 🎯 Установка порога влажности (1400–1407)
      else if (act->action >= 1400 && act->action <= 1407) {
        int ind = act->action - 1400;
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 100) {
            act->action = 0;
            myConfig.chanel[ind].border = num;
            needUpdate = true;
            command = "/Borders";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 100) % !"), userID);
            return;
          }
        }
      }
      // 📝 Переименование датчика (1200–1207)
      else if (act->action >= 1200 && act->action <= 1207) {
        int ind = act->action - 1200;
        strcpy(myConfig.chanel[ind].title, text.c_str());
        command = F("/Namings");
        act->action = 0;
        needUpdate = true;
      }
      // 🚰 Установка режима работы клапана (1300–1307)
      else if (act->action >= 1300 && act->action <= 1307) {
        int ind = act->action - 1300;
        if (text == "✅ ВКЛ.") {
          sendReconnectMessage(F("✅ Клапан включён!"), userID, true);
          myConfig.chanel[ind].mode = 1;
          valve_open(ind);
        } else if (text == "⛔ ВЫКЛ.") {
          sendReconnectMessage(F("⛔ Клапан выключен!"), userID, true);
          myConfig.chanel[ind].mode = 2;
          valve_close(ind);
        } else if (text == "🤖 АВТО") {
          sendReconnectMessage(F("🤖 Клапан в автоматическом режиме!"), userID, true);
          myConfig.chanel[ind].mode = 0;
        } else if (text == "🏠 А.П.") {
          sendReconnectMessage(F("🏠 Клапан в автоматическом режиме для парника!"), userID, true);
          myConfig.chanel[ind].mode = 3;
        }
        needUpdate = true;
        act->action = 0;
        command = F("/OperationMode");
      }
      // 🚰 Установка режима для ВСЕХ клапанов (1399)
      else if (act->action == 1399) {
        int md = 0;
        if (text == "✅ ВКЛ.") {
          sendReconnectMessage(F("✅ Клапаны включены!"), userID, true);
          md = 1;
        } else if (text == "⛔ ВЫКЛ.") {
          sendReconnectMessage(F("⛔ Клапаны выключены!"), userID, true);
          md = 2;
        } else if (text == "🤖 АВТО") {
          sendReconnectMessage(F("🤖 Клапаны в автоматическом режиме!"), userID, true);
          md = 0;
        } else if (text == "🏠 А.П.") {
          sendReconnectMessage(F("🏠 Клапаны в автоматическом режиме для парника!"), userID, true);
          md = 3;
        }
        for (int l = 0; l < 8; l++) {
          myConfig.chanel[l].mode = md;
          if (md == 1) {
            valve_open(l);
          }
          if (md == 2) {
            valve_close(l);
          }
        }
        needUpdate = true;
        act->action = 0;
        command = F("/OperationMode");
      }
      // 🔧 Ручная калибровка (1800–1807)
      else if (act->action >= 1800 && act->action <= 1807) {
        int ind = act->action - 1800;
        String minvs = getValue(text, ',', 0);
        String maxvs = getValue(text, ',', 1);

        if (isNumeric(minvs) && isNumeric(maxvs)) {
          int minv = minvs.toInt();
          int maxv = maxvs.toInt();
          if (minv >= 0 && minv <= 4096 && maxv >= 0 && maxv <= 4096) {
            act->action = 0;
            hs.setLowHighValue(ind, minv, maxv);
            command = "/CalibrateManual";
            myConfig.chanel[ind].maxVal = hs.getHigh(ind);
            myConfig.chanel[ind].minVal = hs.getLow(ind);
            needUpdate = true;
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 4096)!"), userID);
            return;
          }
        } else {
          sendReconnectMessage(F("❌ Ожидалось значение в формате (целое,целое)!"), userID);
          return;
        }
      }
      // 🔄 Сброс настроек (2000)
      else if (act->action == 2000) {
        act->action = 0;
        if (text == "✅ ДА") {
          myConfig.deltaCalibration = 15;
          myConfig.deltaHum = 5;
          myConfig.runOnNight = false;
          myConfig.runOnRain = true;
          hs.setBorder(myConfig.deltaCalibration);
          for (int i = 0; i < 8; i++) {
            myConfig.chanel[i].border = 60;
            myConfig.chanel[i].maxVal = 1024;
            myConfig.chanel[i].minVal = 1024;
            myConfig.chanel[i].mode = 0;
            strcpy(myConfig.chanel[i].title, String("🌱 Растение").c_str());
          }
          sendReconnectMessage(F("✅ Сброс выполнен!"), userID, true);
        } else {
          sendReconnectMessage(F("❌ Сброс отменён!"), userID, true);
        }
        command = "/Configure";
        needUpdate = true;
      }
      // 🔄 Перезагрузка (1)
      else if (act->action == 1) {
        act->action = 0;
        if (text == "✅ ДА") {
          res = 1;
          sendReconnectMessage(F("🔄 Перезагрузка начата!"), userID, true);
          return;
        } else {
          sendReconnectMessage(F("❌ Перезагрузка отменена!"), userID, true);
          return;
        }
      }
      // Стереь все данные по проливу
      else if (act->action == 4000) {
        act->action = 0;
        if (text == "✅ ДА") {
          clearDataFlow();
          sendReconnectMessage(F("🔄 Данные стерты!"), userID, true);
          return;
        } else {
          sendReconnectMessage(F("❌ Отмена!"), userID, true);
          return;
        }
      }
      // 🌙 Работа ночью (1001)
      else if (act->action == 1001) {
        act->action = 0;
        if (text == "✅ ДА") {
          myConfig.runOnNight = true;
          sendReconnectMessage(F("🌙 Работа ночью включена!"), userID, true);
        } else {
          myConfig.runOnNight = false;
          sendReconnectMessage(F("🌙 Работа ночью выключена!"), userID, true);
        }
        command = "/Configure";
        needUpdate = true;
      }
      // 🌧️ Работа под дождём (1002)
      else if (act->action == 1002) {
        act->action = 0;
        if (text == "✅ ДА") {
          myConfig.runOnRain = true;
          sendReconnectMessage(F("🌧️ Работа под дождём включена!"), userID);
        } else {
          myConfig.runOnRain = false;
          sendReconnectMessage(F("🌧️ Работа под дождём выключена!"), userID);
        }
        command = "/Configure";
        needUpdate = true;
      }
      // 📁 Отправка файла по дате (5000)
      else if (act->action == 5000) {
        String input = String(text);
        String sd = getValue(input, '.', 0);
        String sm = getValue(input, '.', 1);
        String sy = getValue(input, '.', 2);

        String fn = String("/") + sy + String("/") + sm + String("/") + sd + String(".csv");

        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            sendReconnectMessage(F("❌ Файл не открывается"), userID);
            Serial.println("can not read file");
          } else {
            sendReconnectMessage(F("📁 Файл открывается, ждите ..."), userID);
            fn.replace("/", "_");

            fb::File fmsg(fn, fb::File::Type::document, file);
            fmsg.chatID = chatID;
            bot.sendFile(fmsg);
          }
          file.close();
          return;
        } else {
          sendReconnectMessage(F("❌ Файл не найден"), userID);
          command = "/reports";
        }
        act->action = 0;
      }
      // 📊 График за период (5200)
      else if (act->action == 5200) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 1 && num <= 60) {
            act->action = 0;
            fileToGrafPeriod(num, userID);
            command = "/Graphics";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 60)!"), userID);
            return;
          }
        } else {
          Serial.println(F("❌ Input error"));
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
      // 📊 График за конкретную дату (5100)
      else if (act->action == 5100) {
        String input = String(text);
        String sd = getValue(input, '.', 0);
        String sm = getValue(input, '.', 1);
        String sy = getValue(input, '.', 2);

        String fn = String("/") + sy + String("/") + sm + String("/") + sd + String(".csv");

        if (SD.exists(fn)) {
          fileToGraf(fn, userID);
        } else {
          sendReconnectMessage(F("❌ Файл не найден"), userID);
        }
        act->action = 0;
        command = "/Graphics";
      }
      // 🗑️ Удаление папки года (1005)
      else if (act->action == 1005) {
        String input = String(text);
        if (isNumeric(input)) {
          Datime t = getDateTime();
          if (t.year == input.toInt()) {
            Serial.println(F("❌ Input error"));
            sendReconnectMessage(F("❌ Удалять текущий год запрещено!"), userID);
            return;
          }
          String del = "/" + input;
          if (!SD.exists(del)) {
            Serial.println(F("❌ Input error"));
            sendReconnectMessage(F("❌ Записей запрашиваемого года не найдено!"), userID);
            act->action = 0;
            command = "/Configure";
          } else {
            File dir = SD.open(del);
            sendReconnectMessage(F("🗑️ Началось удаление файлов, ожидайте..."), userID);
            rm(dir, del + "/");
            dir.close();
            SD.rmdir(del);
            command = "/Configure";
            act->action = 0;
          }
        } else {
          Serial.println(F("❌ Input error"));
          sendReconnectMessage(F("❌ Ожидался ввод года!"), userID);
          return;
        }
      }
      // 💧 Дельта влажности (1003)
      else if (act->action == 1003) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 100) {
            act->action = 0;
            myConfig.deltaHum = num;
            needUpdate = true;
            command = "/Configure";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 100) % !"), userID);
            return;
          }
        } else {
          Serial.println(F("❌ Input error"));
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
      // 🔧 Дельта калибровки (1004)
      else if (act->action == 1004) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 2048) {
            act->action = 0;
            myConfig.deltaCalibration = num;
            needUpdate = true;
            hs.setBorder(myConfig.deltaCalibration);
            command = "/Configure";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 2048)!"), userID);
            return;
          }
        } else {
          Serial.println(F("❌ Input error"));
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
    }

    // ============================================================
    // 📤 OTA ОБНОВЛЕНИЕ (только для владельца системы)
    // ============================================================
    // 📝 Проверяем наличие документа (файла) в сообщении
    if (msg.hasDocument() && check_user->role < 1) {
      fb::DocumentRead doc = msg.document();
      String fileName = doc.name().toString();
      if (fileName == "DripIrrigation.ino.bin" || fileName == "DripIrrigation.ino.bin.gz") {
        // 📥 Загружаем и обновляем прошивку
        bot.updateFlash(u.message().document(), u.message().chat().id());
        return;
      } else {
        sendReconnectMessage("⛔ Только владелец системы может отправлять обновления устройства", chatID);
      }
    } else if (msg.hasDocument()) {
      sendReconnectMessage("⛔ Только владелец системы может отправлять обновления устройства", chatID);
    }

    // ============================================================
    // 📋 ОБРАБОТКА КОМАНД (начинающихся с /)
    // ============================================================
    if (command[0] == '/') {
      // 👑 Команды только для владельца (role == 0)
      if (check_user->role == 0) {
        // (в данной версии нет специфичных команд только для владельца)
      }

      // 🎭 Команды для администраторов (role < 2)
      if (check_user->role < 2) {
        // 🔄 Сброс всех настроек
        if (command == "/DropSettings") {
          sendReconnectMessage(F("⚠️ Сбросить все настройки в значение по умолчанию?"), userID);

          fb::Keyboard kb;
          kb.addButton("✅ ДА").addButton("❌ НЕТ");
          fb::Message m;
          m.text = "<Сброс>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, 2000);
        }
        // 🔧 Ручная калибровка конкретного датчика
        else if (command.startsWith("/HumidityMCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          sendReconnectMessage("🔧 Введите минимальное и максимальное значение датчика № " + String(ind + 1) + " в формате: целое,целое.\nТекущее: [" + String(hs.getLow(ind)) + "; " + String(hs.getHigh(ind)) + "]", userID);
          actionSet(userID, 1800 + ind);
        }
        // 🔧 Автоматическая калибровка конкретного датчика
        else if (command.startsWith("/HumidityCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          sendReconnectMessage("💧 Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\n🚀 Запустить калибровку датчика № " + String((ind + 1)) + "?", userID);

          fb::Keyboard kb;
          kb.addButton("🚀 СТАРТ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Калибровка>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, 1100 + ind);
        }
        // 🔧 Меню калибровки
        else if (command == "/Calibrate") {
          fb::InlineKeyboard menu;
          menu.addButton("🌱 Датчик №1", "/HumidityCalibrate_0", fb::KeyStyle::Primary).newRow()
              .addButton("🌱 Датчик №2", "/HumidityCalibrate_1", fb::KeyStyle::Primary).newRow()
              .addButton("🌱 Датчик №3", "/HumidityCalibrate_2", fb::KeyStyle::Primary).newRow()
              .addButton("🌱 Датчик №4", "/HumidityCalibrate_3", fb::KeyStyle::Primary).newRow()
              .addButton("🌱 Датчик №5", "/HumidityCalibrate_4", fb::KeyStyle::Primary).newRow()
              .addButton("🌱 Датчик №6", "/HumidityCalibrate_5", fb::KeyStyle::Primary).newRow()
              .addButton("🌱 Датчик №7", "/HumidityCalibrate_6", fb::KeyStyle::Primary).newRow()
              .addButton("🌱 Датчик №8", "/HumidityCalibrate_7", fb::KeyStyle::Primary).newRow()
              .addButton("🔙 Назад", "/Configure", fb::KeyStyle::Default);


                    fb::TextEdit t;
          t.mode = fb::Message::Mode::HTML;
          t.text = "🔧 <b>Калибровка</b>";
          t.chatID = u.query().message().chat().id();
          t.messageID = u.query().message().id();         
          t.setKeyboard(&menu);
          bot.editText(t);
        }
        // 🔧 Ручная калибровка (меню)
        else if (command == "/CalibrateManual") {
          hs.setAll();
          fb::InlineKeyboard menu;
          String btnText, cback;
          for (int i = 0; i < 8; i++) {
            btnText = "🌱 Датчик №" + String(i + 1) + " [" + String(hs.getLow(i)) + ";" + String(hs.getHigh(i)) + "] " + String(hs.Percent(i)) + "%";
            cback = "/HumidityMCalibrate_" + String(i);
            menu.addButton(btnText, cback, fb::KeyStyle::Primary);
            if (i < 7) menu.newRow();
          }
          menu.newRow().addButton("🔙 Назад", "/Configure", fb::KeyStyle::Default);

          fb::TextEdit t;
          t.mode = fb::Message::Mode::HTML;
          t.text = "🔧 <b>Ручная Калибровка</b>";
          t.chatID = u.query().message().chat().id();
          t.messageID = u.query().message().id();         
          t.setKeyboard(&menu);
          bot.editText(t);
        }
        // 🗑️ Удаление папки года
        else if (command == "/DelFolder") {
          sendReconnectMessage(F("📅 Введите год удаления (формат YYYY):"), userID);
          actionSet(userID, 1005);
        }
        // 🔧 Дельта калибровки
        else if (command == "/DeltaCalibration") {
          sendReconnectMessage(F("🔧 Введите значение (от 0 до 2048):"), userID);
          actionSet(userID, 1004);
        }
        // 💧 Дельта влажности
        else if (command == "/DeltaHumidity") {
          sendReconnectMessage(F("💧 Введите значение (от 0 до 100) %:"), userID);
          actionSet(userID, 1003);
        }
        // 🌧️ Работа под дождём
        else if (command == "/WorkAtRain") {
          sendReconnectMessage(F("🌧️ Включить режим работы во время дождя?"), userID);

          fb::Keyboard kb;
          kb.addButton("✅ ДА").addButton("❌ НЕТ");
          fb::Message m;
          m.text = "<Включить>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, 1002);
        }
        // 🌙 Работа ночью
        else if (command == "/WorkAtNight") {
          sendReconnectMessage(F("🌙 Включить режим работы в ночное время?"), userID);

          fb::Keyboard kb;
          kb.addButton("✅ ДА").addButton("❌ НЕТ");
          fb::Message m;
          m.text = "<Включить>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, 1001);
        }
        // ⚙️ Главное меню настроек
        else if (command == "/Configure") {
          String menuText = "🔧 <b>Настройка</b>\n"
                          "🌙 Работа ночью " + String(myConfig.runOnNight ? "[✅]" : "[❌]") + "\n"
                          "🌧️ Работа под дождём " + String(myConfig.runOnRain ? "[✅]" : "[❌]") + "\n"
                          "💧 Дельта влажности % (" + String(myConfig.deltaHum) + ")\n"
                          "🔧 Дельта калибровки (" + String(myConfig.deltaCalibration) + ")";

          fb::InlineKeyboard menu;
          menu.addButton("🌙 Работа ночью", "/WorkAtNight", fb::KeyStyle::Primary).newRow()
              .addButton("🌧️ Работа под дождём", "/WorkAtRain", fb::KeyStyle::Primary).newRow()
              .addButton("💧 Дельта влажности", "/DeltaHumidity", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Дельта калибровки", "/DeltaCalibration", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Калибровка", "/Calibrate", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Ручная калибровка", "/CalibrateManual", fb::KeyStyle::Primary).newRow()
              .addButton("🔄 Сброс настроек", "/DropSettings", fb::KeyStyle::Danger).newRow()
              .addButton("🗑️ Удаление файлов", "/DelFolder", fb::KeyStyle::Danger).newRow()
              .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);
/*
          fb::Message m;
          m.text = menuText;
          m.chatID = userID;
          m.setModeHTML();
          m.setKeyboard(&menu);
          bot.sendMessage(m);*/
          fb::TextEdit t;
          t.mode = fb::Message::Mode::HTML;
          t.text = menuText;
          t.chatID = u.query().message().chat().id();
          t.messageID = u.query().message().id();         
          t.setKeyboard(&menu);
          bot.editText(t);
        }
        // 🔄 Перезагрузка системы
        else if (command == "/Restart") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {
            User* user = users.get(key);
            sendReconnectMessage("🔄 Система будет перезагружена!", user->userID);
          }

          fb::Keyboard kb;
          kb.addButton("✅ ДА").addButton("❌ НЕТ");
          fb::Message m;
          m.text = "<Перезагрузка>";
          m.chatID = chatID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, 1);
        }
        // 👥 Меню управления пользователями
        else if (command == "/Users") {
          fb::InlineKeyboard menu;
          menu.addButton("📋 Список", "/UsersList", fb::KeyStyle::Primary).newRow()
              .addButton("⬆️ Повышение", "/UsersUpEdit", fb::KeyStyle::Success).newRow()
              .addButton("⬇️ Понижение", "/UsersDownEdit", fb::KeyStyle::Danger).newRow()
              .addButton("🗑️ Удаление", "/UsersDelete", fb::KeyStyle::Danger).newRow()
              .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);


          fb::TextEdit t;
          t.mode = fb::Message::Mode::HTML;
          t.text = "👥 <b>Пользователи</b>";
          t.chatID = u.query().message().chat().id();
          t.messageID = u.query().message().id();         
          t.setKeyboard(&menu);
          bot.editText(t);

        }
        // 📋 Список пользователей
        else if (command == "/UsersList") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {
            User* user = users.get(key);
            sendReconnectMessage("👤 Пользователь ID: " + user->userID + "\n🎭 Роль: " + user->role, chatID);
          }
        }
        // ⬇️ Меню понижения прав
        else if (command == "/UsersDownEdit") {
          SimpleVector<String> keys = users.keys();
          fb::InlineKeyboard menu;
          bool hasUsers = false;
          for (const String& key : keys) {
            User* user = users.get(key);
            if (user->role == 1) {
              menu.addButton(user->userID, "/DownGradeUser_" + user->userID, fb::KeyStyle::Danger);
              hasUsers = true;
            }
          }
          if (!hasUsers) {
            sendReconnectMessage("ℹ️ Нет пользователей для понижения", userID);
            command = "/Users";
          } else {
            menu.newRow().addButton("🔙 Назад", "/Users", fb::KeyStyle::Default);

            fb::Message m;
            m.text = "⬇️ <b>Понижение</b>";
            m.chatID = userID;
            m.setModeHTML();
            m.setKeyboard(&menu);
            bot.sendMessage(m);
            Serial.println("Show Down user menu");
          }
        }
        // ⬆️ Меню повышения прав
        else if (command == "/UsersUpEdit") {
          SimpleVector<String> keys = users.keys();
          fb::InlineKeyboard menu;
          bool hasUsers = false;
          for (const String& key : keys) {
            User* user = users.get(key);
            if (user->role > 1) {
              menu.addButton(user->userID, "/GradeUser_" + user->userID, fb::KeyStyle::Success);
              hasUsers = true;
            }
          }
          if (!hasUsers) {
            sendReconnectMessage("ℹ️ Нет пользователей для повышения", userID);
            command = "/Users";
          } else {
            menu.newRow().addButton("🔙 Назад", "/Users", fb::KeyStyle::Default);

            fb::Message m;
            m.text = "⬆️ <b>Повышение</b>";
            m.chatID = userID;
            m.setModeHTML();
            m.setKeyboard(&menu);
            bot.sendMessage(m);
            Serial.println("Show Up user menu");
          }
        }
        // 🗑️ Меню удаления пользователей
        else if (command == "/UsersDelete") {
          SimpleVector<String> keys = users.keys();
          fb::InlineKeyboard menu;
          bool hasUsers = false;
          for (const String& key : keys) {
            User* user = users.get(key);
            if (user->role > 0) {
              menu.addButton(user->userID, "/RemoveUser_" + user->userID, fb::KeyStyle::Danger);
              hasUsers = true;
            }
          }
          if (!hasUsers) {
            sendReconnectMessage("ℹ️ Нет пользователей для удаления", userID);
            command = "/Users";
          } else {
            menu.newRow().addButton("🔙 Назад", "/Users", fb::KeyStyle::Default);

            fb::Message m;
            m.text = "🗑️ <b>Удаление</b>";
            m.chatID = userID;
            m.setModeHTML();
            m.setKeyboard(&menu);
            bot.sendMessage(m);
            Serial.println("Show delete user menu");
          }
        }
        // ➕ Добавить пользователя
        else if (command.startsWith("/AddUser")) {
          String userId = getValue(command, '_', 1);
          if (!users.containsKey(userId)) {
            users.put(userId, User(userId, 2));
            sendReconnectMessage("✅ Вас добавил " + username + " в систему как пользователя!", userId);
            sendReconnectMessage("✅ Регистрация пользователя успешно завершена!", chatID);
            saveUsers();
          } else {
            sendReconnectMessage("❌ Данный пользователь уже есть в системе!", chatID);
          }
        }
        // ⬆️ Повысить пользователя до админа
        else if (command.startsWith("/GradeUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role < 2) {
              sendReconnectMessage("ℹ️ Пользователь с ID " + userId + " уже администратор", chatID);
            } else {
              user->role = 1;
              sendReconnectMessage("⬆️ Вас повысил " + username + " — вы теперь администратор!", user->userID);
              sendReconnectMessage("✅ Повышение пользователя " + user->userID + " успешно завершено!", chatID);
              saveUsers();
            }
          } else {
            sendReconnectMessage("❌ Пользователя с ID " + userId + " не найдено", chatID);
          }
        }
        // ⬇️ Понизить пользователя до обычного
        else if (command.startsWith("/DownGradeUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role == 2) {
              sendReconnectMessage("ℹ️ Пользователь с ID " + userId + " уже пользователь", chatID);
            } else {
              user->role = 2;
              sendReconnectMessage("⬇️ Вас понизил " + username + " — вы теперь пользователь!", user->userID);
              sendReconnectMessage("✅ Понижение пользователя " + user->userID + " успешно завершено!", chatID);
              saveUsers();
            }
          } else {
            sendReconnectMessage("❌ Пользователя с ID " + userId + " не найдено", chatID);
          }
        }
        // 🗑️ Удалить пользователя
        else if (command.startsWith("/RemoveUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role == 0) {
              sendReconnectMessage("⛔ Нельзя удалять главного администратора", chatID);
            } else {
              sendReconnectMessage("🗑️ Вас удалил " + username + " из системы!", user->userID);
              sendReconnectMessage("✅ Удаление пользователя " + user->userID + " успешно завершено!", chatID);
              users.remove(user->userID);
            }
          } else {
            sendReconnectMessage("❌ Пользователя с ID " + userId + " не найдено", chatID);
          }
        }
      }

      // ============================================================
      // 📋 КОМАНДЫ ДОСТУПНЫ ВСЕМ ЗАРЕГИСТРИРОВАННЫМ ПОЛЬЗОВАТЕЛЯМ
      // ============================================================
      // 🚰 Пролив дренажа
      if (command == "/Spillage") {
        spillage();
      }
      // 🎯 Установка порога влажности для клапана
      else if (command.startsWith("/BordersSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        sendReconnectMessage(("🎯 Введите % порога срабатывания клапана № " + String((ind + 1)) + ":"), userID);
        actionSet(userID, 1400 + ind);
      }
      // 📝 Переименование датчика
      else if (command.startsWith("/NamingsSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        sendReconnectMessage(("📝 Введите название датчика № " + String((ind + 1)) + ":"), userID);
        actionSet(userID, 1200 + ind);
      }
      // 🚰 Установка режима работы клапана
      else if (command.startsWith("/OperationModeSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        sendReconnectMessage(F("🚰 Выберите режим работы!"), userID);

        fb::Keyboard kb;
        kb.addButton("✅ ВКЛ.").addButton("⛔ ВЫКЛ.").newRow()
            .addButton("🤖 АВТО").addButton("🏠 А.П.");
        fb::Message m;
        m.text = "<Режим>";
        m.chatID = userID;
        m.setKeyboard(&kb);
        bot.sendMessage(m);


        actionSet(userID, 1300 + ind);
      }
      // 🚰 Установка режима для ВСЕХ клапанов
      else if (command == "/AllOperationModeSet") {
        sendReconnectMessage(F("🚰 Выберите режим работы для всех клапанов!"), userID);

        fb::Keyboard kb;
        kb.addButton("✅ ВКЛ.").addButton("⛔ ВЫКЛ.").newRow()
            .addButton("🤖 АВТО").addButton("🏠 А.П.");
        fb::Message m;
        m.text = "<Режим>";
        m.chatID = userID;
        m.setKeyboard(&kb);
        bot.sendMessage(m);

        actionSet(userID, 1399);
      }
      // ⬆️ Запрос на повышение прав
      else if (command == "/GradeMeUp") {
        SimpleVector<String> keys = users.keys();
        for (const String& key : keys) {
          User* user = users.get(key);
          if (user->role < 2) {
            sendReconnectMessage("👤 Пользователь: " + username + " ID: " + userID + " просит поднять его в правах.\n/GradeUser_" + userID, user->userID);
          }
        }
        sendReconnectMessage("⏳ Ваша регистрация принята, ожидайте ответа от администратора", chatID);
      }
      // 🏠 Главное меню
      else if (command == "/Start") {
        if (check_user->role < 2) {
          fb::InlineKeyboard menu;
          menu.addButton("🔄 Перезагрузка", "/Restart", fb::KeyStyle::Danger).newRow()
              .addButton("👥 Пользователи", "/Users", fb::KeyStyle::Primary).newRow()
              .addButton("⚙️ Управление", "/Control", fb::KeyStyle::Primary).newRow()
              .addButton("📊 Статус", "/status", fb::KeyStyle::Primary).newRow()
              .addButton("📈 Отчёты", "/Reports", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Настройка", "/Configure", fb::KeyStyle::Primary);

          fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "🚀 <b>Запуск</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);
        } else {
          fb::InlineKeyboard menu;
          menu.addButton("⚙️ Управление", "/Control", fb::KeyStyle::Primary).newRow()
              .addButton("📊 Статус", "/status", fb::KeyStyle::Primary).newRow()
              .addButton("📈 Отчёты", "/Reports", fb::KeyStyle::Primary);

          fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "🚀 <b>Запуск</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);

        }
      }
      else if (command == "/start") {
        if (check_user->role < 2) {
          fb::InlineKeyboard menu;
          menu.addButton("🔄 Перезагрузка", "/Restart", fb::KeyStyle::Danger).newRow()
              .addButton("👥 Пользователи", "/Users", fb::KeyStyle::Primary).newRow()
              .addButton("⚙️ Управление", "/Control", fb::KeyStyle::Primary).newRow()
              .addButton("📊 Статус", "/status", fb::KeyStyle::Primary).newRow()
              .addButton("📈 Отчёты", "/Reports", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Настройка", "/Configure", fb::KeyStyle::Primary);

          fb::Message m;
          m.text = "🚀 <b>Запуск</b>";
          m.chatID = userID;
          m.setModeHTML();
          m.setKeyboard(&menu);
          bot.sendMessage(m);
        } else {
          fb::InlineKeyboard menu;
          menu.addButton("⚙️ Управление", "/Control", fb::KeyStyle::Primary).newRow()
              .addButton("📊 Статус", "/status", fb::KeyStyle::Primary).newRow()
              .addButton("📈 Отчёты", "/Reports", fb::KeyStyle::Primary);

          fb::Message m;
          m.text = "🚀 <b>Запуск</b>";
          m.chatID = userID;
          m.setModeHTML();
          m.setKeyboard(&menu);
          bot.sendMessage(m);

        }
      }
      // 📝 Меню переименования датчиков
      else if (command == "/Namings") {
        fb::InlineKeyboard menu;
        for (int i = 0; i < 8; i++) {
          String btnText = "🌱 Датчик №" + String(i + 1) + " (" + String(myConfig.chanel[i].title) + ")";
          String cback = "/NamingsSet_" + String(i);
          menu.addButton(btnText, cback, fb::KeyStyle::Primary);
          if (i < 7) menu.newRow();
        }
        menu.newRow().addButton("🔙 Назад", "/Control", fb::KeyStyle::Default);

        fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "📝 <b>Именование</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);
      }
      // 🎯 Меню установки порогов влажности
      else if (command == "/Borders") {
        fb::InlineKeyboard menu;
        for (int i = 0; i < 8; i++) {
          String btnText = "🚰 Клапан №" + String(i + 1) + " (" + String(myConfig.chanel[i].title) + ") <" + String(myConfig.chanel[i].border) + " %>";
          String cback = "/BordersSet_" + String(i);
          menu.addButton(btnText, cback, fb::KeyStyle::Primary);
          if (i < 7) menu.newRow();
        }
        menu.newRow().addButton("🔙 Назад", "/Control", fb::KeyStyle::Default);


        fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "🎯 <b>Пороги срабатывания</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);
      }
      // 🚰 Меню режимов работы клапанов
      else if (command == "/OperationMode") {
        fb::InlineKeyboard menu;
        for (int i = 0; i < 8; i++) {
          String modeSymbol;
          if (myConfig.chanel[i].mode == 0) modeSymbol = "➖";
          else if (myConfig.chanel[i].mode == 1) modeSymbol = "✅";
          else if (myConfig.chanel[i].mode == 2) modeSymbol = "⛔";
          else modeSymbol = "🏠";

          String btnText = "🚰 Клапан №" + String(i + 1) + " (" + String(myConfig.chanel[i].title) + ") [" + modeSymbol + "]";
          String cback = "/OperationModeSet_" + String(i);
          menu.addButton(btnText, cback, fb::KeyStyle::Primary);
          if (i < 7) menu.newRow();
        }
        menu.newRow()
            .addButton("🚰 Установить для всех", "/AllOperationModeSet", fb::KeyStyle::Success).newRow()
            .addButton("🔙 Назад", "/Control", fb::KeyStyle::Default);

       fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "🚰 <b>Режим работы</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);
      }
      // 📊 Вывод текущего статуса системы
      else if (command == "/status") {
        hs.setAll();
        String status = String("ℹ️ <b>Общий статус</b>\n\n");
        status = status + String("📅 <b>Текущая дата и время:</b> ") + getDateTime().toString(' ');
        status = status + String("\n\n") + String(nightNow ? "🌙 Сейчас ночь" : "☀️ Сейчас день");
        status = status + String(rainNow ? ", 🌧️ идёт дождь" : ", ☀️ дождя нет");
        status = status + String("\n");
        status = status + String("\n📊 <b>Информация по датчикам</b>");
        for (int i = 0; i < 8; i++) {
          status = status + String("\n");
          status = status + String("\n🌱 Канал № ") + String((i + 1)) + String(" (") + String(myConfig.chanel[i].title) + String(")");
          if (check_user->role == 0) {
            status = status + String("\n📟 Текущее значение АЦП: ") + String(hs.getCurrent(i));
          }
          status = status + String("\n💧 Текущая влажность: ") + String(hs.Percent(i)) + String(" %");
          status = status + String("\n🎯 Граничное значение: ") + String(myConfig.chanel[i].border) + String(" %");
          status = status + String("\n🚰 Клапан: ") + String((oldMode[i] == 11 || oldMode[i] == 2) ? "⛔ закрыт" : oldMode[i] == 3 ? "➖ без контроля" : "✅ открыт");
          status = status + String("\n⚙️ Режим: ") + String(myConfig.chanel[i].mode == 0 ? "🤖 автоматический" : myConfig.chanel[i].mode == 1 ? "✅ постоянно открыт"
                                                                                      : myConfig.chanel[i].mode == 2 ? "⛔ постоянно закрыт"
                                                                                                                     : "🏠 автоматический (парник)");
        }
        // 💧 Добавляем информацию о расходе воды
        status = status + String("\n");
        status = status + String("\n💧 <b>Расход воды</b>");
        status = status + String("\n📟 За текущую сессию: ") + String(flowGetSessionLiters(), 3) + String(" л");
        status = status + String("\n📊 Общий расход: ") + String(flowGetTotalLiters(), 3) + String(" л");

        Serial.println(ESP.getFreeHeap());
        int mem = ESP.getFreeHeap() / 1024;
        status = status + String("\n");
        status = status + String("\n💾 <b>Оставшаяся память:</b> ") + String(mem) + " Kb";
        sendReconnectMessage(status, userID);
      }
      // 🔍 Поиск датчика
      else if (command == "/Searching") {
        sendReconnectMessage(F("🔍 Произойдёт поиск датчика. Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\n🚀 Запустить поиск датчика?"), userID);

        fb::Keyboard kb;
        kb.addButton("🚀 СТАРТ").addButton("❌ ОТМЕНА");
        fb::Message m;
        m.text = "<Поиск>";
        m.chatID = userID;
        m.setKeyboard(&kb);
        bot.sendMessage(m);

        actionSet(userID, 3000);
      }
      // 💧 Команда отображения расхода воды
      else if (command == "/WaterFlow") {
        String flowMsg = String("💧 <b>Расход воды</b>\n\n");
        flowMsg = flowMsg + String("📟 За текущую сессию полива: ") + String(flowGetSessionLiters(), 3) + String(" л\n");
        flowMsg = flowMsg + String("📊 Общий расход за все время: ") + String(flowGetTotalLiters(), 3) + String(" л\n");
        flowMsg = flowMsg + String("🔄 Импульсов датчика: ") + String(flowPulseCount) + String(" шт.");
        
       
        fb::InlineKeyboard menu;
        menu.addButton("🗑️ Стереть данные пролива", "/WaterSpillage", fb::KeyStyle::Danger);
        
        fb::Message m;
        m.text = flowMsg;
          m.chatID = userID;
          m.setModeHTML();
          m.setKeyboard(&menu);
          bot.sendMessage(m);
      }
      // 💧 Команда отображения расхода воды
      else if (command == "/WaterSpillage") {
          sendReconnectMessage(F("⚠️ Сбросить все данные о проливе в системе?"), userID);

          fb::Keyboard kb;
          kb.addButton("✅ ДА").addButton("❌ НЕТ");
          fb::Message m;
          m.text = "<Сброс>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, 4000);
        
      }
      // ⚙️ Меню управления
      else if (command == "/Control") {
        fb::InlineKeyboard menu;
        menu.addButton("🚰 Режим работы", "/OperationMode", fb::KeyStyle::Primary).newRow()
            .addButton("📝 Названия", "/Namings", fb::KeyStyle::Primary).newRow()
            .addButton("🎯 Пороги срабатывания", "/Borders", fb::KeyStyle::Primary).newRow()
            .addButton("💧 Расход воды", "/WaterFlow", fb::KeyStyle::Primary).newRow()
            .addButton("🗑️ Пролив дренажа", "/Spillage", fb::KeyStyle::Danger).newRow()
            .addButton("🔍 Поиск датчика", "/Searching", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);

          fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "⚙️ <b>Управление</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);
      }
      else if (command == "/control") {
        fb::InlineKeyboard menu;
        menu.addButton("🚰 Режим работы", "/OperationMode", fb::KeyStyle::Primary).newRow()
            .addButton("📝 Названия", "/Namings", fb::KeyStyle::Primary).newRow()
            .addButton("🎯 Пороги срабатывания", "/Borders", fb::KeyStyle::Primary).newRow()
            .addButton("💧 Расход воды", "/WaterFlow", fb::KeyStyle::Primary).newRow()
            .addButton("🗑️ Пролив дренажа", "/Spillage", fb::KeyStyle::Danger).newRow()
            .addButton("🔍 Поиск датчика", "/Searching", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);


          fb::Message m;
          m.text = "⚙️ <b>Управление</b>";
          m.chatID = userID;
          m.setModeHTML();
          m.setKeyboard(&menu);
          bot.sendMessage(m);
      }
      // 🔕 Пауза статусных сообщений
      else if (command == "/pause") {
        check_user->messages = false;
        sendReconnectMessage(F("🔕 Отключены все статусные сообщения"), userID);
      }
      // 🔔 Возобновление статусных сообщений
      else if (command == "/continue") {
        check_user->messages = true;
        sendReconnectMessage(F("🔔 Статусные сообщения активированы"), userID);
      }
      // 📊 График за вчера
      else if (command == "/GraphicsYesterday") {
        Datime t(getDateTime().getUnix() - 60 * 60 * 24);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          fileToGraf(fn, userID);
        } else {
          sendReconnectMessage(F("❌ Файл за вчера не найден"), userID);
        }
        command = "/Graphics";
      }
      // 📊 График за сегодня
      else if (command == "/GraphicsToday") {
        Datime t = getDateTime();
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          fileToGraf(fn, userID);
        } else {
          sendReconnectMessage(F("❌ Файл за сегодня не найден"), userID);
        }
        command = "/Graphics";
      }
      // 📊 График за период (запрос дней)
      else if (command == "/GraphicsPeriod") {
        sendReconnectMessage(F("📅 Введите число дней, за которые хотите отобразить график"), userID);
        actionSet(userID, 5200);
      }
      // 📊 График за конкретную дату
      else if (command == "/GraphicsTo") {
        sendReconnectMessage(F("📅 Введите дату в формате dd.mm.yyyy, за которую хотите отобразить график"), userID);
        actionSet(userID, 5100);
      }
      // 📁 Файл за конкретную дату
      else if (command == "/FileTo") {
        sendReconnectMessage(F("📅 Введите дату в формате dd.mm.yyyy, за которую хотите получить файл"), userID);
        actionSet(userID, 5000);
      }
      // 📁 Файл за сегодня
      else if (command == "/FileToday") {
        Datime t = getDateTime();
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            Serial.println("can not read file");
            sendReconnectMessage(F("❌ Файл не открывается"), userID);
          } else {
            sendReconnectMessage(F("📁 Файл открывается, ждите ..."), userID);
            fn.replace("/", "_");

            fb::File fmsg(fn, fb::File::Type::document, file);
            fmsg.chatID = chatID;
            bot.sendFile(fmsg);
          }
          file.close();
        } else {
          sendReconnectMessage(F("❌ Файл за сегодня не найден"), userID);
          command = "/reports";
        }
      }
      // 📁 Файл за вчера
      else if (command == "/FileYesterday") {
        Datime t(getDateTime().getUnix() - 60 * 60 * 24);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            Serial.println("can not read file");
            sendReconnectMessage(F("❌ Файл не открывается"), userID);
          } else {
            sendReconnectMessage(F("📁 Файл открывается, ждите ..."), userID);
            fn.replace("/", "_");

            fb::File fmsg(fn, fb::File::Type::document, file);
            fmsg.chatID = chatID;
            bot.sendFile(fmsg);
          }
          file.close();
        } else {
          sendReconnectMessage(F("❌ Файл за вчера не найден"), userID);
          command = "/reports";
        }
      }
      // 📊 График за декаду
      else if (command == "/GraphicsDecade") {
        fileToGrafPeriod(10, userID);
        command = "/Graphics";
      }

      // 📈 Меню отчётов
      if (command == "/reports") {
        fb::InlineKeyboard menu;
        menu.addButton("📊 Графики", "/Graphics", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл за вчера", "/FileYesterday", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл текущий", "/FileToday", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл...", "/FileTo", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);


              fb::Message m;
              m.text = "📈 <b>Отчёты</b>";
              m.chatID = userID;
              m.setModeHTML();
              m.setKeyboard(&menu);
              bot.sendMessage(m);

      }

      // 📈 Меню отчётов
      if (command == "/Reports") {
        fb::InlineKeyboard menu;
        menu.addButton("📊 Графики", "/Graphics", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл за вчера", "/FileYesterday", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл текущий", "/FileToday", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл...", "/FileTo", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);



            fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "📈 <b>Отчёты</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);

      }
      // 📊 Меню графиков
      if (command == "/Graphics") {
        fb::InlineKeyboard menu;
        menu.addButton("📊 График за вчера", "/GraphicsYesterday", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График за сегодня", "/GraphicsToday", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График за декаду", "/GraphicsDecade", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График за период", "/GraphicsPeriod", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График...", "/GraphicsTo", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Reports", fb::KeyStyle::Default);


            fb::TextEdit t;
            t.mode = fb::Message::Mode::HTML;
            t.text = "📊 <b>Графики</b>";
            t.chatID = u.query().message().chat().id();
            t.messageID = u.query().message().id();         
            t.setKeyboard(&menu);
            bot.editText(t);
      }
    }
    // ============================================================
    // ❌ НЕКОМАНДНОЕ СООБЩЕНИЕ (не начинается с /)
    // ============================================================
    else {
      fb::Message reply;
      reply.text = "🤖 Бот распознаёт только команды, начинающиеся с символа '/'";
      reply.chatID = chatID;
      reply.reply.messageID = msg.id().toInt();
      bot.sendMessage(reply);
    }
  }
  // ============================================================
  // 👤 НЕЗАРЕГИСТРИРОВАННЫЙ ПОЛЬЗОВАТЕЛЬ
  // ============================================================
  else {
    if (text == "/register") {
      SimpleVector<String> keys = users.keys();
      for (const String& key : keys) {
        User* user = users.get(key);
        if (user->role < 2) {
          sendReconnectMessage("👤 Пользователь: " + username + " ID: " + userID + " просит зарегистрировать его.\n/AddUser_" + userID, user->userID);
        }
      }
      sendReconnectMessage("⏳ Ваша регистрация принята, ожидайте ответа от администратора", chatID);
    } else {
      fb::Message reply;
      reply.text = "🔒 Только зарегистрированные пользователи могут общаться со мной. Если хотите зарегистрироваться — пришлите запрос: /register";
      reply.chatID = chatID;
      reply.reply.messageID = msg.id().toInt();
      bot.sendMessage(reply);
    }
  }
}
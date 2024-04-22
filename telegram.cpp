#include "telegram.h"
#include "objects.h"
#include "hashtable.h"
#include <EEPROM.h>
#include <CharPlot.h>



void newMsg(FB_msg& msg);
void loadUsers();

int64_t getUnixTime() {
  if (bot.timeSynced()) {
    return bot.getUnix() + 3600 * 3;
  } else {
    DateTime now = rtc.now();
    return now.unixtime();
  }
}


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

String IntWith2Zero(int data) {
  String s = String(data);
  if (data < 10) { s = String("0") + s; }
  return s;
}


void fileToGrafPeriod(int period, int del, String msgID) {

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

  int64_t ut = getUnixTime() - 60 * 60 * 24 * period;

  for (int p = 0; p < period; p++) {
    int64_t cut = ut + 60 * 60 * 24 * p;
    FB_Time t(cut, 0);
    String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
    Serial.print("Try get file :");
    Serial.println(fn);
    if (SD.exists(fn)) {
      File printFile = SD.open(fn, FILE_READ);

      if (!printFile) {
        Serial.print("The text file cannot be opened");
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

  bot.setTextMode(FB_MARKDOWN);
  for (int i = 0; i < 8; i++) {
    bot.sendMessage(String("```\nКанал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X1>(arr[i], sz, 10) + String("\n```"), msgID);
  }
  bot.setTextMode(FB_TEXT);
}


void fileToGraf(String fn, String msgID) {

  File printFile = SD.open(fn, FILE_READ);

  if (!printFile) {
    Serial.print("The text file cannot be opened");
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
    FB_Time t(ut, 0);
    if (t.hour > ind[index]) {
      if (ind[index] >= 0) {
        arr[index][ind[index]] = value[index] * 1.0 / count[index];
        value[index] = 0;
        count[index] = 0;
      }
      ind[index] = t.hour;
    }

    value[index] += curr.toInt();
    count[index]++;

    //do some action here
  }
  for (int i = 0; i < 8; i++) {
    if (ind[i] >= 0) {
      arr[i][ind[i]] = value[i] * 1.0 / count[i];
    }
  }
  printFile.close();
  bot.setTextMode(FB_MARKDOWN);
  for (int i = 0; i < 8; i++) {
    bot.sendMessage(String("```\nКанал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X1>(arr[i], sz, 10) + String("\n```"), msgID);
  }
  bot.setTextMode(FB_TEXT);
}

void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void timeFixed() {
  if (bot.timeSynced()) {
    FB_Time t = bot.getTime(3);
    Serial.println("Time Date");
    Serial.print(t.timeString());
    Serial.print(' ');
    Serial.println(t.dateString());
    DateTime now = rtc.now();

    int64_t t1 = now.unixtime();
    int64_t t2 = bot.getUnix() + 3600 * 3;

    if (abs(t1 - t2) > 9) {
      rtc.adjust(DateTime(t.year, t.month, t.day, t.hour, t.minute, t.second));
      Serial.println("RTS Adjusting");
    }
  }
}

void botInit() {
  bot.attach(newMsg);
  loadUsers();
  timeFixed();
}



class Action {
public:
  String userID;
  int action;

  Action(const String& u, int a)
    : userID(u), action(a) {}
};

class User {
public:
  String userID;
  byte role;
  bool messages = true;
  User(const String& u, byte r)
    : userID(u), role(r) {}
};

struct SaveUser {
  char userID[12] = "";
  byte role = 0;
};



Hashtable<String, User> users;
Hashtable<String, Action> actions;

void saveUsers() {
  Serial.println("begin write users");
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

void reConnection() {
  Serial.println("try send reconnect message");
  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    Serial.print("Send message for user: ");
    Serial.println(user->userID);
    bot.sendMessage("Система снова в сети!", user->userID);
  }
  timeFixed();
}

void dropCDCard() {
  Serial.println("try send disconnect CD CARD message");
  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    Serial.print("Send message for user: ");
    Serial.println(user->userID);
    bot.sendMessage(F("Отсутствует CD карта система не сможет работать!"), user->userID);
  }
}

void sendStatus(String text) {
  if (dropped) return;

  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    if (user->messages) {
      bot.sendMessage(text, user->userID);
    }
  }
}

void actionSet(String userID, int action) {
  if (actions.containsKey(userID)) {
    Action* act = actions.get(userID);
    act->action = action;
  } else {
    actions.put(userID, Action(userID, action));
  }
}
void connectCDCard() {
  Serial.println("try send CD CARD Connected message");
  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    Serial.print("Send message for user: ");
    Serial.println(user->userID);
    bot.sendMessage(F("CD карта снова активна!"), user->userID);
  }
}

void loadUsers() {
  EEPROM.begin(4096);
  int ind = 0;
  EEPROM.get(250, ind);
  Serial.println("begin read users");
  Serial.println("count users = " + String(ind));
  for (int i = 0; i < ind; i++) {
    SaveUser usr;
    int shift = 255 + i * sizeof(SaveUser);
    Serial.println("EEPROM shift " + String(shift));
    EEPROM.get(shift, usr);
    users.put(String(usr.userID), User(String(usr.userID), usr.role));
    Serial.println("read user UserID = " + String(usr.userID) + " role = " + String(usr.role));
    bot.sendMessage("Система запущенна!", usr.userID);
    if (usr.role < 2) {
      String menu = F("Перезагрузка \n Пользователи \n Управление \n Статус \n Настройка");
      String cback = F("/Restart,/Users,/control,/status,/Configure");
      bot.inlineMenuCallback("<Запуск>", menu, cback, usr.userID);
    } else {
      String menu = F("Управление \n Статус ");
      String cback = F("/control,/status");
      bot.inlineMenuCallback("<Запуск>", menu, cback, usr.userID);
    }
  }
  EEPROM.end();
}

int search[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

// обработчик сообщений
void newMsg(FB_msg& msg) {
  Serial.print(msg.chatID);  // ID чата
  Serial.print(", ");
  Serial.print(msg.username);  // логин
  Serial.print(", ");
  Serial.print(msg.text);  // текст
  Serial.print(", ");
  Serial.print(msg.data);  // текст
  Serial.print(", ");
  Serial.println(msg.query);  // текст


  if (users.elements() == 0) {
    if (msg.text == String(tstr)) {
      bot.replyMessage(F("Привет, владелец системы!"), msg.messageID, msg.chatID);
      users.put(msg.userID, User(msg.userID, 0));
      saveUsers();
    } else {
      bot.replyMessage(F("Первый запрос должен быть с ключевым словом, полученным при настройке Wifi"), msg.messageID, msg.chatID);
    }
    return;
  }
  if (users.containsKey(msg.userID)) {
    User* check_user = users.get(msg.userID);
    String command = "";
    if (msg.text[0] == '<') {
      command = msg.data;
    } else
      command = msg.text;

    if (actions.containsKey(msg.userID)) {
      Action* act = actions.get(msg.userID);
      if (command.startsWith("/")) {
        act->action = 0;
      } else if (act->action >= 1130 && act->action <= 1137) {
        int ind = act->action - 1130;
        if (msg.text == "ЗАВЕРШИТЬ") {
          if (abs(hs.getHigh(ind) - hs.getLow(ind)) < 100) {
            bot.sendMessage("Ошибка калибровки датчик № " + String((ind + 1)) + " слишком малое значение!\nОтменяем...", msg.userID);
            hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
            command = F("/Calibrate");
          }
          bot.sendMessage("Калибровка завершена датчик № " + String((ind + 1)) + " полностью функционален!", msg.userID);
          myConfig.chanel[ind].maxVal = hs.getHigh(ind);
          myConfig.chanel[ind].minVal = hs.getLow(ind);
          data.update();
          act->action = 0;
          command = F("/Calibrate");
        } else {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action >= 1120 && act->action <= 1127) {
        int ind = act->action - 1120;
        if (msg.text == "ДАЛЕЕ") {
          hs.setAll();
          int val = hs.setHigh(ind);
          Serial.print("Сухое значение: ");
          Serial.println(val);
          bot.sendMessage("Установите датчик № " + String((ind + 1)) + " в почву и нажмите завершить!", msg.userID);
          bot.showMenuText("<Калибровка>", "ЗАВЕРШИТЬ \t ОТМЕНА", msg.userID, true);
          act->action = 1130 + ind;
          return;
        } else {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action >= 1110 && act->action <= 1117) {
        int ind = act->action - 1110;
        if (msg.text == "ДАЛЕЕ") {
          hs.setAll();
          int val = hs.setLow(ind);
          Serial.print("Значение с водой датчик ");
          Serial.print(ind);
          Serial.print(": ");
          Serial.println(val);
          act->action = 1120 + ind;
          bot.sendMessage("Достаньте датчик № " + String((ind + 1)) + " из воды протрите и нажмите далее!", msg.userID);
          bot.showMenuText("<Калибровка>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          return;
        } else {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action >= 1100 && act->action <= 1107) {
        if (msg.text == "СТАРТ") {
          int ind = act->action - 1100;
          bot.sendMessage("Положите датчик № " + String((ind + 1)) + " в воду и нажмите далее!", msg.userID);
          bot.showMenuText("<Калибровка>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          act->action = 1110 + ind;
          return;
        } else {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action == 3000) {
        if (msg.text == "СТАРТ") {
          bot.sendMessage("Положите датчик в воду и нажмите далее!", msg.userID);
          bot.showMenuText("<Поиск>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          act->action = 3010;
          return;
        } else {
          bot.sendMessage(F("Поиск отменён!"), msg.userID);
          command = F("/control");
        }
        act->action = 0;
      } else if (act->action == 3010) {
        if (msg.text == "ДАЛЕЕ") {
          hs.setAll();
          for (int i = 0; i < 8; i++) {
            search[i] = hs.getCurrent(i);
          }
          bot.sendMessage("Достаньте датчик из воды протрите и нажмите далее!", msg.userID);
          bot.showMenuText("<Поиск>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          act->action = 3020;
          return;
        } else {
          bot.sendMessage(F("Поиск отменён!"), msg.userID);
          command = F("/control");
        }
        act->action = 0;
      } else if (act->action == 3020) {
        if (msg.text == "ДАЛЕЕ") {
          hs.setAll();
          int ind = -1;
          for (int i = 0; i < 8; i++) {
            if (abs(search[i] - hs.getCurrent(i)) > 300) {
              ind = i;
              break;
            }
          }
          if (ind < 0) {
            bot.sendMessage(F("Не удалось определить датчик! Повторите операцию."), msg.userID);
          } else {
            bot.sendMessage("Предположительно ваш датчик № " + String(ind) + " (" + myConfig.chanel[ind].title + ")!", msg.userID);
          }
        } else {
          bot.sendMessage(F("Поиск отменён!"), msg.userID);
        }
        command = F("/control");
        act->action = 0;
      } else if (act->action >= 1400 && act->action <= 1407) {
        int ind = act->action - 1400;
        String input = String(msg.text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 100) {
            act->action = 0;
            myConfig.chanel[ind].border = num;
            data.update();
            command = "/Borders";
          } else {
            bot.sendMessage(F("Ожидалось значение (от 0 до 100) % !"), msg.userID);
            return;
          }
        }
      } else if (act->action >= 1200 && act->action <= 1207) {
        int ind = act->action - 1200;
        strcpy(myConfig.chanel[ind].title, msg.text.c_str());
        command = F("/Namings");
        act->action = 0;
        data.update();
      } else if (act->action >= 1300 && act->action <= 1307) {
        int ind = act->action - 1300;
        if (msg.text == "ВКЛ.") {
          bot.sendMessage(F("Клапан включён!"), msg.userID);
          myConfig.chanel[ind].mode = 1;
        } else if (msg.text == "ВЫКЛ.") {
          bot.sendMessage(F("Клапан выключен!"), msg.userID);
          myConfig.chanel[ind].mode = 2;
        } else if (msg.text == "АВТО") {
          bot.sendMessage(F("Клапан в ароматическом режиме!"), msg.userID);
          myConfig.chanel[ind].mode = 0;
        }
        data.update();
        act->action = 0;
        command = F("/OperationMode");
      } else if (act->action == 2000) {
        act->action = 0;
        if (msg.text == "ДА") {
          myConfig.deltaCalibration = 15;
          myConfig.deltaHum = 5;
          myConfig.runOnNight = false;
          myConfig.runOnRain = true;
          for (int i = 0; i < 8; i++) {
            myConfig.chanel[i].border = 60;
            myConfig.chanel[i].maxVal = 1024;
            myConfig.chanel[i].minVal = 1024;
            myConfig.chanel[i].mode = 0;
            strcpy(myConfig.chanel[i].title, String("Растение").c_str());
          }
          bot.sendMessage(F("Сброс выполнен!"), msg.userID);
        } else {
          bot.sendMessage(F("Сброс отменён!"), msg.userID);
        }
        command = "/Configure";
        data.update();
      } else if (act->action == 1) {
        act->action = 0;
        if (msg.text == "ДА") {
          res = 1;
          bot.sendMessage(F("Перезагрузка начата!"), msg.userID);
          return;
        } else {
          bot.sendMessage(F("Перезагрузка отменена!"), msg.userID);
          return;
        }
      } else if (act->action == 1001) {
        act->action = 0;
        if (msg.text == "ДА") {
          myConfig.runOnNight = true;
          bot.sendMessage(F("Работа ночью включена!"), msg.userID);
        } else {
          myConfig.runOnNight = false;
          bot.sendMessage(F("Работа ночью выключена!"), msg.userID);
        }
        command = "/Configure";
        data.update();
      } else if (act->action == 1002) {
        act->action = 0;
        if (msg.text == "ДА") {
          myConfig.runOnRain = true;
          bot.sendMessage(F("Работа под дождём включена!"), msg.userID);
        } else {
          myConfig.runOnRain = false;
          bot.sendMessage(F("Работа под дождём выключена!"), msg.userID);
        }
        command = "/Configure";
        data.update();
      } else if (act->action == 5000) {
        String input = String(msg.text);
        String sd = getValue(input, '.', 0);
        String sm = getValue(input, '.', 1);
        String sy = getValue(input, '.', 2);

        String fn = String("/") + sy + String("/") + sm + String("/") + sd + String(".csv");

        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            Serial.println("can not read file");
          } else {
            fn.replace("/", "_");
            bot.sendFile(file, FB_DOC, fn, msg.chatID);
          }
          file.close();
          return;
        } else {
          bot.sendMessage(F("Файл не найден"), msg.userID);
          command = "/Reports";
        }
        act->action = 0;
      } else if (act->action == 5100) {
        String input = String(msg.text);
        String sd = getValue(input, '.', 0);
        String sm = getValue(input, '.', 1);
        String sy = getValue(input, '.', 2);

        String fn = String("/") + sy + String("/") + sm + String("/") + sd + String(".csv");

        if (SD.exists(fn)) {
          fileToGraf(fn, msg.userID);
        } else {
          bot.sendMessage(F("Файл не найден"), msg.userID);
        }
        act->action = 0;
        command = "/Graphics";
      } else if (act->action == 1003) {
        String input = String(msg.text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 100) {
            act->action = 0;
            myConfig.deltaHum = num;
            data.update();
            command = "/Configure";
          } else {
            bot.sendMessage(F("Ожидалось значение (от 0 до 100) % !"), msg.userID);
            return;
          }
        } else {
          Serial.println(F("Input error"));
          bot.sendMessage(F("Ожидалось ввод целого числа повторите!"), msg.userID);
          return;
        }
      } else if (act->action == 1004) {
        String input = String(msg.text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 2048) {
            act->action = 0;
            myConfig.deltaCalibration = num;
            data.update();
            hs.setBorder(myConfig.deltaCalibration);
            command = "/Configure";
          } else {
            bot.sendMessage(F("Ожидался значение (от 0 до 2048)!"), msg.userID);
            return;
          }
        } else {
          Serial.println(F("Input error"));
          bot.sendMessage(F("Ожидался ввод целого числа повторите!"), msg.userID);
          return;
        }
      }
    }

    if (msg.OTA && check_user->role < 1 && msg.fileName == "update.bin") {
      bot.update();
      return;
    } else {
      if (msg.OTA) bot.sendMessage("Только владельцы системы могут отправлять обновления устройства", msg.chatID);
    }

    if (command[0] == '/') {
      if (check_user->role == 0) {
      }
      if (check_user->role < 2) {
        if (command == "/DropSettings") {
          bot.sendMessage(F("Сбросить все настройки в значение по умолчанию!"), msg.userID);
          bot.showMenuText("<Сброс>", "ДА \t НЕТ", msg.userID, true);
          actionSet(msg.userID, 2000);
        } else if (command.startsWith("/HumidityCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          bot.sendMessage("Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\nЗапустить калибровку датчика № " + String((ind + 1)) + "?", msg.userID);
          bot.showMenuText("<Калибровка>", "СТАРТ \t ОТМЕНА", msg.userID, true);
          actionSet(msg.userID, 1100 + ind);
        } else if (command == "/Calibrate") {
          String menu = F("Датчик влажности № 1 \n Датчик влажности № 2 \n Датчик влажности № 3 \n Датчик влажности № 4 \n Датчик влажности № 5 \n Датчик влажности № 6 \n Датчик влажности № 7 \n Датчик влажности № 8 \n Назад");
          String cback = F("/HumidityCalibrate_0,/HumidityCalibrate_1,/HumidityCalibrate_2,/HumidityCalibrate_3,/HumidityCalibrate_4,/HumidityCalibrate_5,/HumidityCalibrate_6,/HumidityCalibrate_7,/Configure");
          bot.inlineMenuCallback("<Калибровка>", menu, cback, msg.userID);
        } else if (command == "/DeltaCalibration") {
          bot.sendMessage(F("Введите значение (от 0 до 2048):"), msg.userID);
          actionSet(msg.userID, 1004);
        } else if (command == "/DeltaHumidity") {
          bot.sendMessage(F("Введите значение (от 0 до 100) %:"), msg.userID);
          actionSet(msg.userID, 1003);
        } else if (command == "/WorkAtRain") {
          bot.sendMessage(F("Включить режим работы во время дождя!"), msg.userID);
          bot.showMenuText("<Включить>", "ДА \t НЕТ", msg.userID, true);
          actionSet(msg.userID, 1002);
        } else if (command == "/WorkAtNight") {
          bot.sendMessage(F("Включить режим работы в ночное время!"), msg.userID);
          bot.showMenuText("<Включить>", "ДА \t НЕТ", msg.userID, true);
          actionSet(msg.userID, 1001);
        } else if (command == "/Configure") {
          ///TODO сделать конфигурацию в зависимости от датчиков
          String menu = ("Работа ночью " + String(myConfig.runOnNight ? "[x]" : "[o]") + " \n Работа под дождём " + String(myConfig.runOnRain ? "[x]" : "[o]") + " \n Дельта влажности % (" + String(myConfig.deltaHum) + ") \n Дельта калибровки (" + String(myConfig.deltaCalibration) + ") \n Калибровка \n Сброс настроек \n Назад");
          String cback = F("/WorkAtNight,/WorkAtRain,/DeltaHumidity,/DeltaCalibration,/Calibrate,/DropSettings,/reset");
          bot.inlineMenuCallback("<Настройка>", menu, cback, msg.userID);
        } else if (command == "/Restart") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {
            User* user = users.get(key);
            bot.sendMessage("Система будет перезагружена!", user->userID);
          }
          // res = 1;
          bot.showMenuText("<Перезагрузка>", "ДА \t НЕТ", msg.chatID, true);
          actionSet(msg.userID, 1);
        } else if (command == "/Users") {
          String menu = F("Список \n Повышение \n Понижение \n Удаление \n Назад");
          String cback = F("/UsersList,/UsersUpEdit,/UsersDownEdit,/UsersDelete,/reset");
          bot.inlineMenuCallback("<Пользователи>", menu, cback, msg.userID);

        } else if (command == "/UsersList") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {
            User* user = users.get(key);
            bot.sendMessage("Пользователь йд: " + user->userID + ". Роль: " + user->role, msg.chatID);
          }
          //if (msg.query == 1) bot.answer("Выполнено", FB_NOTIF);
        } else if (command == "/UsersDownEdit") {
          SimpleVector<String> keys = users.keys();
          String menu = "";
          String cback = "";
          for (const String& key : keys) {
            User* user = users.get(key);
            if (user->role == 1) {
              menu += user->userID + F(" \n ");
              cback += "/DownGradeUser_" + user->userID + F(",");
            }
          }
          menu += "Назад";
          cback += "/Users";
          bot.inlineMenuCallback("<Понижение>", menu, cback, msg.userID);

          Serial.println("Show Down user menu");
        } else if (command == "/UsersUpEdit") {
          SimpleVector<String> keys = users.keys();
          String menu = "";
          String cback = "";
          for (const String& key : keys) {
            User* user = users.get(key);
            if (user->role > 1) {
              menu += user->userID + F(" \n ");
              cback += "/GradeUser_" + user->userID + F(",");
            }
          }
          menu += " Назад ";
          cback += "/Users";
          bot.inlineMenuCallback("<Повышение>", menu, cback, msg.userID);

          Serial.println("Show Up user menu");
        } else if (command == "/UsersDelete") {
          SimpleVector<String> keys = users.keys();
          String menu = "";
          String cback = "";
          for (const String& key : keys) {
            User* user = users.get(key);
            if (user->role > 0) {
              menu += user->userID + F(" \n ");
              cback += "/RemoveUser_" + user->userID + F(",");
            }
          }
          menu += "Назад";
          cback += "/Users";
          bot.inlineMenuCallback("<Удаление>", menu, cback, msg.userID);

          Serial.println("Show delete user menu");
        } else if (command.startsWith("/AddUser")) {
          String userId = getValue(command, '_', 1);
          if (!users.containsKey(userId)) {
            users.put(userId, User(userId, 2));
            bot.sendMessage("Вас добавил " + msg.username + " в систему как пользователя!", userId);
            bot.sendMessage("Регистрация пользователя успешно завершена!", msg.chatID);
            saveUsers();
          } else {
            bot.sendMessage("Данный пользователь уже есть в системе!", msg.chatID);
          }
        } else if (command.startsWith("/GradeUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role < 2) {
              bot.sendMessage("Пользователя с таким йд " + userId + " уже администратор", msg.chatID);
            } else {
              user->role = 1;
              bot.sendMessage("Вас повысил " + msg.username + " в правах, вы теперь администратор!", user->userID);
              bot.sendMessage("Повышение пользователя " + user->userID + " успешно завершено!", msg.chatID);
              //users.put(user.userID, user);
              saveUsers();
            }

          } else {
            bot.sendMessage("Пользователя с таким йд " + userId + " не найдено", msg.chatID);
          }
        } else if (command.startsWith("/DownGradeUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role == 2) {
              bot.sendMessage("Пользователя с таким йд " + userId + " уже пользователь", msg.chatID);
            } else {
              user->role = 2;
              bot.sendMessage("Вас понизил " + msg.username + " в правах, вы теперь пользователь!", user->userID);
              bot.sendMessage("Понижение пользователя " + user->userID + " успешно завершено!", msg.chatID);
              //users.put(user.userID, user);
              saveUsers();
            }
          } else {
            bot.sendMessage("Пользователя с таким йд " + userId + " не найдено", msg.chatID);
          }
        } else if (command.startsWith("/RemoveUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role == 0) {
              bot.sendMessage("Нельзя удалять главного администратора", msg.chatID);
            } else {
              bot.sendMessage("Вас удалил " + msg.username + " из системы!", user->userID);
              bot.sendMessage("Удаление пользователя " + user->userID + " успешно завершено!", msg.chatID);
              users.remove(user->userID);
            }
          } else {
            bot.sendMessage("Пользователя с таким йд " + userId + " не найдено", msg.chatID);
          }
        }
      }
      if (command.startsWith("/BordersSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        bot.sendMessage(("Введите % порога срабатывания клапана № " + String((ind + 1)) + ":"), msg.userID);
        actionSet(msg.userID, 1400 + ind);
      } else if (command.startsWith("/NamingsSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        bot.sendMessage(("Введите название датчика № " + String((ind + 1)) + ":"), msg.userID);
        actionSet(msg.userID, 1200 + ind);
      } else if (command.startsWith("/OperationModeSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        bot.sendMessage(F("Выберите режим работы!"), msg.userID);
        bot.showMenuText("<Режим>", "ВКЛ. \t ВЫКЛ. \t АВТО", msg.userID, true);
        actionSet(msg.userID, 1300 + ind);
      } else if (command == "/GradeMeUp") {
        SimpleVector<String> keys = users.keys();
        for (const String& key : keys) {
          User* user = users.get(key);
          if (user->role < 2) {
            bot.sendMessage("Пользователь: " + msg.username + " йд: " + msg.userID + ". Просит поднять его в правах. /GradeUser_" + msg.userID, user->userID);
          }
        }
        bot.sendMessage("Ваша регистрация принята, ожидайте ответа от Администратора", msg.chatID);
      } else if (command == "/reset") {
        if (check_user->role < 2) {
          String menu = F("Перезагрузка \n Пользователи \n Управление \n Статус \n Настройка");
          String cback = F("/Restart,/Users,/control,/status,/Configure");
          bot.inlineMenuCallback("<Запуск>", menu, cback, msg.userID);
        } else {
          String menu = F("Управление \n Статус ");
          String cback = F("/control,/status");
          bot.inlineMenuCallback("<Запуск>", menu, cback, msg.userID);
        }
      } else if (command == "/Namings") {
        String menu = "Датчик влажности № 1 (" + String(myConfig.chanel[0].title)
                      + ") \n Датчик влажности № 2 (" + String(myConfig.chanel[1].title)
                      + ") \n Датчик влажности № 3 (" + String(myConfig.chanel[2].title)
                      + ") \n Датчик влажности № 4 (" + String(myConfig.chanel[3].title)
                      + ") \n Датчик влажности № 5 (" + String(myConfig.chanel[4].title)
                      + ") \n Датчик влажности № 6 (" + String(myConfig.chanel[5].title)
                      + ") \n Датчик влажности № 7 (" + String(myConfig.chanel[6].title)
                      + ") \n Датчик влажности № 8 (" + String(myConfig.chanel[7].title) + ") \n Назад";
        String cback = F("/NamingsSet_0,/NamingsSet_1,/NamingsSet_2,/NamingsSet_3,/NamingsSet_4,/NamingsSet_5,/NamingsSet_6,/NamingsSet_7,/control");
        bot.inlineMenuCallback("<Калибровка>", menu, cback, msg.userID);
      } else if (command == "/Borders") {
        String menu = "Клапан № 1 (" + String(myConfig.chanel[0].title)
                      + ") <" + String(myConfig.chanel[0].border)
                      + " %> \n Клапан № 2 (" + String(myConfig.chanel[1].title)
                      + ")  <" + String(myConfig.chanel[1].border)
                      + " %> \n Клапан № 3 (" + String(myConfig.chanel[2].title)
                      + ")  <" + String(myConfig.chanel[2].border)
                      + " %> \n Клапан № 4 (" + String(myConfig.chanel[3].title)
                      + ")  <" + String(myConfig.chanel[3].border)
                      + " %> \n Клапан № 5 (" + String(myConfig.chanel[4].title)
                      + ")  <" + String(myConfig.chanel[4].border)
                      + " %> \n Клапан № 6 (" + String(myConfig.chanel[5].title)
                      + ")  <" + String(myConfig.chanel[5].border)
                      + " %> \n Клапан № 7 (" + String(myConfig.chanel[6].title)
                      + ")  <" + String(myConfig.chanel[6].border)
                      + " %> \n Клапан № 8 (" + String(myConfig.chanel[7].title)
                      + ")  <" + String(myConfig.chanel[7].border)
                      + " %>  \n Назад";
        String cback = F("/BordersSet_0,/BordersSet_1,/BordersSet_2,/BordersSet_3,/BordersSet_4,/BordersSet_5,/BordersSet_6,/BordersSet_7,/control");
        bot.inlineMenuCallback("<Режим работы>", menu, cback, msg.userID);
      } else if (command == "/OperationMode") {
        String menu = "Клапан № 1 (" + String(myConfig.chanel[0].title)
                      + ") [" + String(myConfig.chanel[0].mode == 0 ? "-" : myConfig.chanel[0].mode == 1 ? "x"
                                                                                                         : "o")
                      + "] \n Клапан № 2 (" + String(myConfig.chanel[1].title)
                      + ") [" + String(myConfig.chanel[1].mode == 0 ? "-" : myConfig.chanel[1].mode == 1 ? "x"
                                                                                                         : "o")
                      + "] \n Клапан № 3 (" + String(myConfig.chanel[2].title)
                      + ") [" + String(myConfig.chanel[2].mode == 0 ? "-" : myConfig.chanel[2].mode == 1 ? "x"
                                                                                                         : "o")
                      + "] \n Клапан № 4 (" + String(myConfig.chanel[3].title)
                      + ") [" + String(myConfig.chanel[3].mode == 0 ? "-" : myConfig.chanel[3].mode == 1 ? "x"
                                                                                                         : "o")
                      + "] \n Клапан № 5 (" + String(myConfig.chanel[4].title)
                      + ") [" + String(myConfig.chanel[4].mode == 0 ? "-" : myConfig.chanel[4].mode == 1 ? "x"
                                                                                                         : "o")
                      + "] \n Клапан № 6 (" + String(myConfig.chanel[5].title)
                      + ") [" + String(myConfig.chanel[5].mode == 0 ? "-" : myConfig.chanel[5].mode == 1 ? "x"
                                                                                                         : "o")
                      + "] \n Клапан № 7 (" + String(myConfig.chanel[6].title)
                      + ") [" + String(myConfig.chanel[6].mode == 0 ? "-" : myConfig.chanel[6].mode == 1 ? "x"
                                                                                                         : "o")
                      + "] \n Клапан № 8 (" + String(myConfig.chanel[7].title)
                      + ") [" + String(myConfig.chanel[7].mode == 0 ? "-" : myConfig.chanel[7].mode == 1 ? "x"
                                                                                                         : "o")
                      + "]  \n Назад";
        String cback = F("/OperationModeSet_0,/OperationModeSet_1,/OperationModeSet_2,/OperationModeSet_3,/OperationModeSet_4,/OperationModeSet_5,/OperationModeSet_6,/OperationModeSet_7,/control");
        bot.inlineMenuCallback("<Режим работы>", menu, cback, msg.userID);
      } else if (command == "/status") {
        String status = String(nightNow ? "Сейчас ночь" : "Сейчас день");
        status = status + String(rainNow ? ", идёт дождь" : ", дождя нет");
        status = status + String("\n");
        status = status + String("\n") + String("Информация по датчикам");
        for (int i = 0; i < 8; i++) {
          status = status + String("\n");
          status = status + String("\n") + String("Канал № ") + String((i + 1)) + String(" (") + String(myConfig.chanel[i].title) + String(")");
          status = status + String("\n") + String("Текущая влажность: ") + String(hs.Percent(i)) + String(" %");
          status = status + String("\n") + String("Граничное значение: ") + String(myConfig.chanel[i].border) + String(" %");
          status = status + String("\n") + String("Клапан: ") + String((oldMode[i] == 11 || oldMode[i] == 2) ? "закрыт" : "открыт");
          status = status + String("\n") + String("Режим: ") + String(myConfig.chanel[i].mode == 0 ? "автоматический" : myConfig.chanel[i].mode == 1 ? "постоянно открыт"
                                                                                                                                                     : "постоянно закрыт");
        }
        Serial.println(ESP.getFreeHeap());
        int mem = ESP.getFreeHeap() / 1024;
        status = status + String("\n");
        status = status + String("\n") + "Оставшаяся память : " + String(mem) + " Kb";
        bot.sendMessage(status, msg.userID);
      } else if (command == "/Searching") {
        bot.sendMessage(F("Произойдет поиск датчика. Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\nЗапустить поиск датчика?"), msg.userID);
        bot.showMenuText("<Поиск>", "СТАРТ \t ОТМЕНА", msg.userID, true);
        actionSet(msg.userID, 3000);
      } else if (command == "/control") {
        String menu = F("Режим работы \n Названия \n Пороги срабатывания \n Поиск датчика \n Отчеты \n Назад");
        String cback = F("/OperationMode,/Namings,/Borders,/Searching,/Reports,/reset");
        bot.inlineMenuCallback("<Управление>", menu, cback, msg.userID);
      } else if (command == "/pause") {
        check_user->messages = false;
        bot.sendMessage(F("Отключены все статусные сообщения"), msg.userID);
      } else if (command == "/continue") {
        check_user->messages = true;
        bot.sendMessage(F("Статусные сообщения активированы"), msg.userID);
      } else if (command == "/GraphicsYesterday") {
        FB_Time t(getUnixTime() - 60 * 60 * 24, 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          fileToGraf(fn, msg.userID);
        } else {
          bot.sendMessage(F("Файл за вчера не найден"), msg.userID);
        }
        command = "/Graphics";
      } else if (command == "/GraphicsToday") {
        FB_Time t(getUnixTime(), 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          fileToGraf(fn, msg.userID);
        } else {
          bot.sendMessage(F("Файл за сегодня не найден"), msg.userID);
        }
        command = "/Graphics";
      } else if (command == "/GraphicsTo") {
        bot.sendMessage(F("Введите дату в формате dd.mm.yyyy за которую хотите отобразить график"), msg.userID);
        actionSet(msg.userID, 5100);
      } else if (command == "/FileTo") {
        bot.sendMessage(F("Введите дату в формате dd.mm.yyyy за которую хотите получить файл"), msg.userID);
        actionSet(msg.userID, 5000);
      } else if (command == "/FileToday") {
        FB_Time t(getUnixTime(), 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            Serial.println("can not read file");
          } else {
            fn.replace("/", "_");
            bot.sendFile(file, FB_DOC, fn, msg.chatID);
          }
          file.close();
        } else {
          bot.sendMessage(F("Файл за сегодня не найден"), msg.userID);
          command = "/Reports";
        }
      } else if (command == "/FileYesterday") {
        FB_Time t(getUnixTime() - 60 * 60 * 24, 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            Serial.println("can not read file");
          } else {
            fn.replace("/", "_");
            bot.sendFile(file, FB_DOC, fn, msg.chatID);
          }
          file.close();
        } else {
          bot.sendMessage(F("Файл за вчера не найден"), msg.userID);
          command = "/Reports";
        }
      } else if (command == "/GraphicsDecade") {
        fileToGrafPeriod(10, 3, msg.userID);
        command = "/Reports";
      } else if (command == "/Reports") {
        String menu = F(" Графики \n  Файл за вчера \n Файл текущий \n Файл... \n Назад");
        String cback = F("/Graphics,/FileYesterday,/FileToday,/FileTo,/control");
        bot.inlineMenuCallback("<Отчеты>", menu, cback, msg.userID);
      }
      if (command == "/Graphics") {
        String menu = F(" График за вчера \n График за сегодня \n График за декаду \n График... \n Назад");
        String cback = F("/GraphicsYesterday,/GraphicsToday,/GraphicsDecade,/GraphicsTo,/Reports");
        bot.inlineMenuCallback("<Отчеты>", menu, cback, msg.userID);
      }
    } else {
      bot.replyMessage("Бот распознает только команды начинающиеся с символа - '/'", msg.messageID, msg.chatID);
    }
  } else {
    if (msg.text == "/register") {
      SimpleVector<String> keys = users.keys();
      for (const String& key : keys) {
        User* user = users.get(key);
        if (user->role < 2) {
          bot.sendMessage("Пользователь: " + msg.username + " йд: " + msg.userID + ". Просит его зарегистрировать. /AddUser_" + msg.userID, user->userID);
        }
      }
      bot.sendMessage("Ваша регистрация принята, ожидайте ответа от Администратора", msg.chatID);
    } else {
      bot.replyMessage("Только зарегистрированные пользователи могут общаться со мной, если хотите зарегистрироваться пришлите запрос на регистрацию /register", msg.messageID, msg.chatID);
    }
  }
}

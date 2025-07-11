#include "telegram.h"
#include "objects.h"
#include "hashtable.h"
#include <EEPROM.h>
#include <CharPlot.h>
#include "valves.h"

bool needUpdate = false;
bool telegram_needUpdate() {
  bool nu = needUpdate;
  needUpdate = false;
  return nu;
}

void sendReconnectMessage(String text, String id) {
  for (int i = 0; i < 3; i++) {
    int r = bot.sendMessage(text, id);

    if (r != 1) {
      WiFi.disconnect();
      delay(10);
      Serial.println("Status wi-fi is broken");
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
      dropped = true;
    } else break;
  }
}

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
          Serial.print("Deleted folder ");
          Serial.println(localPath);
        } else {
          Serial.print("Unable to delete folder ");
          Serial.println(localPath);
        }
      } else {
        localPath = tempPath + entry.name();

        if (SD.remove(localPath)) {
          Serial.print("Deleted ");
          Serial.println(localPath);
        } else {
          Serial.print("Failed to delete ");
          Serial.println(localPath);
        }
      }
    } else {
      // break out of recursion
      break;
    }
  }
}

void newMsg(FB_msg& msg);
void loadUsers();



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


void fileToGrafPeriod(int period, String msgID) {
  sendReconnectMessage(F("Началась генерация отчетов, ждите ..."), msgID);

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
    sendReconnectMessage(String("```\nКанал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X2>(arr[i], sz, 10) + String("\n```"), msgID);
  }
  bot.setTextMode(FB_TEXT);
}


void fileToGraf(String fn, String msgID) {
  sendReconnectMessage(F("Началась генерация отчетов, ждите ..."), msgID);
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
    sendReconnectMessage(String("```\nКанал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X1>(arr[i], sz, 10) + String("\n```"), msgID);
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
  bot.skipUpdates();
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

void reConnection(unsigned long time) {

  Serial.println("try send reconnect message");
  if (time > 300 * 1000) {
    SimpleVector<String> keys = users.keys();
    for (const String& key : keys) {
      User* user = users.get(key);
      Serial.print("Send message for user: ");
      Serial.println(user->userID);
      sendReconnectMessage("Система снова в сети!", user->userID);
    }
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
    sendReconnectMessage(F("Отсутствует CD карта система не сможет работать!"), user->userID);
  }
}



void sendStatus(String text) {
  if (dropped) return;

  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    if (user->messages) {
      sendReconnectMessage(text, user->userID);
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
    sendReconnectMessage(F("CD карта снова активна!"), user->userID);
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
    sendReconnectMessage("Система запущенна!", usr.userID);
    if (usr.role < 2) {
      String menu = F(" Перезагрузка \n Пользователи \n Управление \n Статус \n Отчеты \n Настройка ");
      String cback = F("/Restart,/Users,/control,/status,/reports,/Configure");
      bot.inlineMenuCallback("<Запуск>", menu, cback, usr.userID);
    } else {
      String menu = F(" Управление \n Статус \n Отчеты ");
      String cback = F("/control,/status,/reports");
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
            sendReconnectMessage("Ошибка калибровки датчик № " + String((ind + 1)) + " слишком малое значение!\nОтменяем...", msg.userID);
            hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
            command = F("/Calibrate");
          }
          sendReconnectMessage("Калибровка завершена датчик № " + String((ind + 1)) + " полностью функционален!", msg.userID);
          myConfig.chanel[ind].maxVal = hs.getHigh(ind);
          myConfig.chanel[ind].minVal = hs.getLow(ind);
          needUpdate = true;
          act->action = 0;
          command = F("/Calibrate");
        } else {
          sendReconnectMessage(F("Калибровка отменена!"), msg.userID);
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
          sendReconnectMessage("Установите датчик № " + String((ind + 1)) + " в почву и нажмите завершить!", msg.userID);
          bot.showMenuText("<Калибровка>", "ЗАВЕРШИТЬ \t ОТМЕНА", msg.userID, true);
          act->action = 1130 + ind;
          return;
        } else {
          sendReconnectMessage(F("Калибровка отменена!"), msg.userID);
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
          sendReconnectMessage("Достаньте датчик № " + String((ind + 1)) + " из воды протрите и нажмите далее!", msg.userID);
          bot.showMenuText("<Калибровка>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          return;
        } else {
          sendReconnectMessage(F("Калибровка отменена!"), msg.userID);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action >= 1100 && act->action <= 1107) {
        if (msg.text == "СТАРТ") {
          int ind = act->action - 1100;
          sendReconnectMessage("Положите датчик № " + String((ind + 1)) + " в воду и нажмите далее!", msg.userID);
          bot.showMenuText("<Калибровка>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          act->action = 1110 + ind;
          return;
        } else {
          sendReconnectMessage(F("Калибровка отменена!"), msg.userID);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action == 3000) {
        if (msg.text == "СТАРТ") {
          sendReconnectMessage("Положите датчик в воду и нажмите далее!", msg.userID);
          bot.showMenuText("<Поиск>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          act->action = 3010;
          return;
        } else {
          sendReconnectMessage(F("Поиск отменён!"), msg.userID);
          command = F("/control");
        }
        act->action = 0;
      } else if (act->action == 3010) {
        if (msg.text == "ДАЛЕЕ") {
          hs.setAll();
          for (int i = 0; i < 8; i++) {
            search[i] = hs.getCurrent(i);
          }
          sendReconnectMessage("Достаньте датчик из воды протрите и нажмите далее!", msg.userID);
          bot.showMenuText("<Поиск>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          act->action = 3020;
          return;
        } else {
          sendReconnectMessage(F("Поиск отменён!"), msg.userID);
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
            sendReconnectMessage(F("Не удалось определить датчик! Повторите операцию."), msg.userID);
          } else {
            sendReconnectMessage("Предположительно ваш датчик № " + String(ind + 1) + " (" + myConfig.chanel[ind].title + ")!", msg.userID);
          }
        } else {
          sendReconnectMessage(F("Поиск отменён!"), msg.userID);
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
            needUpdate = true;
            command = "/Borders";
          } else {
            sendReconnectMessage(F("Ожидалось значение (от 0 до 100) % !"), msg.userID);
            return;
          }
        }
      } else if (act->action >= 1200 && act->action <= 1207) {
        int ind = act->action - 1200;
        strcpy(myConfig.chanel[ind].title, msg.text.c_str());
        command = F("/Namings");
        act->action = 0;
        needUpdate = true;
      } else if (act->action >= 1300 && act->action <= 1307) {
        int ind = act->action - 1300;
        if (msg.text == "ВКЛ.") {
          sendReconnectMessage(F("Клапан включён!"), msg.userID);
          myConfig.chanel[ind].mode = 1;
          valve_open(ind);
        } else if (msg.text == "ВЫКЛ.") {
          sendReconnectMessage(F("Клапан выключен!"), msg.userID);
          myConfig.chanel[ind].mode = 2;
          valve_close(ind);
        } else if (msg.text == "АВТО") {
          sendReconnectMessage(F("Клапан в ароматическом режиме!"), msg.userID);
          myConfig.chanel[ind].mode = 0;
        } else if (msg.text == "А.П.") {
          sendReconnectMessage(F("Клапан в ароматическом режиме для парника!"), msg.userID);
          myConfig.chanel[ind].mode = 3;
        }
        needUpdate = true;
        act->action = 0;
        command = F("/OperationMode");
      } else if (act->action == 1399) {        
        int md = 0;
        if (msg.text == "ВКЛ.") {
          sendReconnectMessage(F("Клапаны включены!"), msg.userID);
          md = 1;          
        } else if (msg.text == "ВЫКЛ.") {
          sendReconnectMessage(F("Клапаны выключены!"), msg.userID);
          md = 2;
        } else if (msg.text == "АВТО") {
          sendReconnectMessage(F("Клапаны в ароматическом режиме!"), msg.userID);
          md = 0;
        } else if (msg.text == "А.П.") {
          sendReconnectMessage(F("Клапаны в ароматическом режиме для парника!"), msg.userID);
          md = 3;
        }     
        for(int l = 0; l < 8; l ++){
            myConfig.chanel[l].mode = md;
            if (md == 1){
              valve_open(l);
            }
            if (md == 2){
              valve_close(l);
            }
        }
        needUpdate = true;
        act->action = 0;
        command = F("/OperationMode");
      } else if (act->action >= 1800 && act->action <= 1807) {
        int ind = act->action - 1800;
        String minvs = getValue(msg.text, ',', 0);
        String maxvs = getValue(msg.text, ',', 1);

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
            sendReconnectMessage(F("Ожидалось значение (от 0 до 4096) % !"), msg.userID);
            return;
          }
        } else {
          sendReconnectMessage(F("Ожидалось значение в формате (целое,целое)!"), msg.userID);
          return;
        }
      } else if (act->action == 2000) {
        act->action = 0;
        if (msg.text == "ДА") {
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
            strcpy(myConfig.chanel[i].title, String("Растение").c_str());
          }
          sendReconnectMessage(F("Сброс выполнен!"), msg.userID);
        } else {
          sendReconnectMessage(F("Сброс отменён!"), msg.userID);
        }
        command = "/Configure";
        needUpdate = true;
      } else if (act->action == 1) {
        act->action = 0;
        if (msg.text == "ДА") {
          res = 1;
          sendReconnectMessage(F("Перезагрузка начата!"), msg.userID);
          return;
        } else {
          sendReconnectMessage(F("Перезагрузка отменена!"), msg.userID);
          return;
        }
      } else if (act->action == 1001) {
        act->action = 0;
        if (msg.text == "ДА") {
          myConfig.runOnNight = true;
          sendReconnectMessage(F("Работа ночью включена!"), msg.userID);
        } else {
          myConfig.runOnNight = false;
          sendReconnectMessage(F("Работа ночью выключена!"), msg.userID);
        }
        command = "/Configure";
        needUpdate = true;
      } else if (act->action == 1002) {
        act->action = 0;
        if (msg.text == "ДА") {
          myConfig.runOnRain = true;
          sendReconnectMessage(F("Работа под дождём включена!"), msg.userID);
        } else {
          myConfig.runOnRain = false;
          sendReconnectMessage(F("Работа под дождём выключена!"), msg.userID);
        }
        command = "/Configure";
        needUpdate = true;
      } else if (act->action == 5000) {
        String input = String(msg.text);
        String sd = getValue(input, '.', 0);
        String sm = getValue(input, '.', 1);
        String sy = getValue(input, '.', 2);

        String fn = String("/") + sy + String("/") + sm + String("/") + sd + String(".csv");

        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            sendReconnectMessage(F("Файл не открывается"), msg.userID);
            Serial.println("can not read file");
          } else {
            sendReconnectMessage(F("Файл открывается, ждите ..."), msg.userID);
            fn.replace("/", "_");
            bot.sendFile(file, FB_DOC, fn, msg.chatID);
          }
          file.close();
          return;
        } else {
          sendReconnectMessage(F("Файл не найден"), msg.userID);
          command = "/reports";
        }
        act->action = 0;
      } else if (act->action == 5200) {
        String input = String(msg.text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 1 && num <= 60) {
            act->action = 0;
            fileToGrafPeriod(num, msg.userID);
            command = "/Graphics";
          } else {
            sendReconnectMessage(F("Ожидалось значение (от 0 до 60)!"), msg.userID);
            return;
          }
        } else {
          Serial.println(F("Input error"));
          sendReconnectMessage(F("Ожидался ввод целого числа повторите!"), msg.userID);
          return;
        }
      } else if (act->action == 5100) {
        String input = String(msg.text);
        String sd = getValue(input, '.', 0);
        String sm = getValue(input, '.', 1);
        String sy = getValue(input, '.', 2);

        String fn = String("/") + sy + String("/") + sm + String("/") + sd + String(".csv");

        if (SD.exists(fn)) {
          fileToGraf(fn, msg.userID);
        } else {
          sendReconnectMessage(F("Файл не найден"), msg.userID);
        }
        act->action = 0;
        command = "/Graphics";
      } else if (act->action == 1005) {
        String input = String(msg.text);
        if (isNumeric(input)) {

          FB_Time t(getUnixTime(), 0);
          if (t.year == input.toInt()) {
            Serial.println(F("Input error"));
            sendReconnectMessage(F("Удалять текущий год запрещено!"), msg.userID);
            return;
          }
          String del = "/" + input;
          if (!SD.exists(del)) {
            Serial.println(F("Input error"));
            sendReconnectMessage(F("Записей запрашиваемого года не найдено!"), msg.userID);
            act->action = 0;
            command = "/Configure";
          } else {
            File dir = SD.open(del);
            sendReconnectMessage(F("Началось удаление файлов, ожидайте..."), msg.userID);
            rm(dir, del + "/");
            dir.close();
            SD.rmdir(del);
            command = "/Configure";
            act->action = 0;
          }
        } else {
          Serial.println(F("Input error"));
          sendReconnectMessage(F("Ожидался ввод года!"), msg.userID);
          return;
        }
      } else if (act->action == 1003) {
        String input = String(msg.text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 100) {
            act->action = 0;
            myConfig.deltaHum = num;
            needUpdate = true;
            command = "/Configure";
          } else {
            sendReconnectMessage(F("Ожидалось значение (от 0 до 100) % !"), msg.userID);
            return;
          }
        } else {
          Serial.println(F("Input error"));
          sendReconnectMessage(F("Ожидалось ввод целого числа повторите!"), msg.userID);
          return;
        }
      } else if (act->action == 1004) {
        String input = String(msg.text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 2048) {
            act->action = 0;
            myConfig.deltaCalibration = num;
            needUpdate = true;
            hs.setBorder(myConfig.deltaCalibration);
            command = "/Configure";
          } else {
            sendReconnectMessage(F("Ожидалось значение (от 0 до 2048)!"), msg.userID);
            return;
          }
        } else {
          Serial.println(F("Input error"));
          sendReconnectMessage(F("Ожидался ввод целого числа повторите!"), msg.userID);
          return;
        }
      }
    }

    if (msg.OTA && check_user->role < 1 && (msg.fileName == "DripIrrigation.ino.bin" || msg.fileName == "DripIrrigation.ino.bin.gz")) {
      bot.update();
      return;
    } else {
      if (msg.OTA) sendReconnectMessage("Только владельцы системы могут отправлять обновления устройства", msg.chatID);
    }

    if (command[0] == '/') {
      if (check_user->role == 0) {
      }
      if (check_user->role < 2) {
        if (command == "/DropSettings") {
          sendReconnectMessage(F("Сбросить все настройки в значение по умолчанию!"), msg.userID);
          bot.showMenuText("<Сброс>", "ДА \t НЕТ", msg.userID, true);
          actionSet(msg.userID, 2000);
        } else if (command.startsWith("/HumidityMCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          sendReconnectMessage("Введите минимальное и максимальное значение датчика № " + String(ind + 1) + " в формате: целое,целое, текущее: [" + String(hs.getLow(ind)) + "; " + String(hs.getHigh(ind)) + "].", msg.userID);
          actionSet(msg.userID, 1800 + ind);
        } else if (command.startsWith("/HumidityCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          sendReconnectMessage("Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\nЗапустить калибровку датчика № " + String((ind + 1)) + "?", msg.userID);
          bot.showMenuText("<Калибровка>", "СТАРТ \t ОТМЕНА", msg.userID, true);
          actionSet(msg.userID, 1100 + ind);
        } else if (command == "/Calibrate") {
          String menu = F(" Датчик влажности № 1 \n Датчик влажности № 2 \n Датчик влажности № 3 \n Датчик влажности № 4 \n Датчик влажности № 5 \n Датчик влажности № 6 \n Датчик влажности № 7 \n Датчик влажности № 8 \n Назад ");
          String cback = F("/HumidityCalibrate_0,/HumidityCalibrate_1,/HumidityCalibrate_2,/HumidityCalibrate_3,/HumidityCalibrate_4,/HumidityCalibrate_5,/HumidityCalibrate_6,/HumidityCalibrate_7,/Configure");
          bot.inlineMenuCallback("<Калибровка>", menu, cback, msg.userID);
        } else if (command == "/CalibrateManual") {
          hs.setAll();
          String menu = " Датчик влажности № 1 [" + String(hs.getLow(0)) + ";" + String(hs.getHigh(0))
                        + "] " + String(hs.Percent(0)) + "% - " + String(hs.getCurrent(0)) + " \n Датчик влажности № 2 [" + String(hs.getLow(1)) + ";" + String(hs.getHigh(1))
                        + "] " + String(hs.Percent(1)) + "% - " + String(hs.getCurrent(1)) + " \n Датчик влажности № 3 [" + String(hs.getLow(2)) + ";" + String(hs.getHigh(2))
                        + "] " + String(hs.Percent(2)) + "% - " + String(hs.getCurrent(2)) + " \n Датчик влажности № 4 [" + String(hs.getLow(3)) + ";" + String(hs.getHigh(3))
                        + "] " + String(hs.Percent(3)) + "% - " + String(hs.getCurrent(3)) + " \n Датчик влажности № 5 [" + String(hs.getLow(4)) + ";" + String(hs.getHigh(4))
                        + "] " + String(hs.Percent(4)) + "% - " + String(hs.getCurrent(4)) + " \n Датчик влажности № 6 [" + String(hs.getLow(5)) + ";" + String(hs.getHigh(5))
                        + "] " + String(hs.Percent(5)) + "% - " + String(hs.getCurrent(5)) + " \n Датчик влажности № 7 [" + String(hs.getLow(6)) + ";" + String(hs.getHigh(6))
                        + "] " + String(hs.Percent(6)) + "% - " + String(hs.getCurrent(6)) + " \n Датчик влажности № 8 [" + String(hs.getLow(7)) + ";" + String(hs.getHigh(7))
                        + "] " + String(hs.Percent(7)) + "% - " + String(hs.getCurrent(7)) + " \n Назад ";
          String cback = F("/HumidityMCalibrate_0,/HumidityMCalibrate_1,/HumidityMCalibrate_2,/HumidityMCalibrate_3,/HumidityMCalibrate_4,/HumidityMCalibrate_5,/HumidityMCalibrate_6,/HumidityMCalibrate_7,/Configure");
          bot.inlineMenuCallback("<Ручная Калибровка>", menu, cback, msg.userID);
        } else if (command == "/DelFolder") {
          sendReconnectMessage(F("Введите год удаления (формат YYYY):"), msg.userID);
          actionSet(msg.userID, 1005);
        } else if (command == "/DeltaCalibration") {
          sendReconnectMessage(F("Введите значение (от 0 до 2048):"), msg.userID);
          actionSet(msg.userID, 1004);
        } else if (command == "/DeltaHumidity") {
          sendReconnectMessage(F("Введите значение (от 0 до 100) %:"), msg.userID);
          actionSet(msg.userID, 1003);
        } else if (command == "/WorkAtRain") {
          sendReconnectMessage(F("Включить режим работы во время дождя!"), msg.userID);
          bot.showMenuText("<Включить>", "ДА \t НЕТ", msg.userID, true);
          actionSet(msg.userID, 1002);
        } else if (command == "/WorkAtNight") {
          sendReconnectMessage(F("Включить режим работы в ночное время!"), msg.userID);
          bot.showMenuText("<Включить>", "ДА \t НЕТ", msg.userID, true);
          actionSet(msg.userID, 1001);
        } else if (command == "/Configure") {
          ///TODO сделать конфигурацию в зависимости от датчиков
          String menu = (" Работа ночью " + String(myConfig.runOnNight ? "[x]" : "[o]") + " \n Работа под дождём " + String(myConfig.runOnRain ? "[x]" : "[o]") + " \n Дельта влажности % (" + String(myConfig.deltaHum) + ") \n Дельта калибровки (" + String(myConfig.deltaCalibration) + ") \n Калибровка  \n Ручная калибровка \n Сброс настроек \n Удаление файлов \n Назад ");
          String cback = F("/WorkAtNight,/WorkAtRain,/DeltaHumidity,/DeltaCalibration,/Calibrate,/CalibrateManual,/DropSettings,/DelFolder,/start");
          bot.inlineMenuCallback("<Настройка>", menu, cback, msg.userID);
        } else if (command == "/Restart") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {
            User* user = users.get(key);
            sendReconnectMessage("Система будет перезагружена!", user->userID);
          }
          // res = 1;
          bot.showMenuText("<Перезагрузка>", "ДА \t НЕТ", msg.chatID, true);
          actionSet(msg.userID, 1);
        } else if (command == "/Users") {
          String menu = F(" Список \n Повышение \n Понижение \n Удаление \n Назад ");
          String cback = F("/UsersList,/UsersUpEdit,/UsersDownEdit,/UsersDelete,/start");
          bot.inlineMenuCallback("<Пользователи>", menu, cback, msg.userID);

        } else if (command == "/UsersList") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {
            User* user = users.get(key);
            sendReconnectMessage("Пользователь йд: " + user->userID + ". Роль: " + user->role, msg.chatID);
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
            sendReconnectMessage("Вас добавил " + msg.username + " в систему как пользователя!", userId);
            sendReconnectMessage("Регистрация пользователя успешно завершена!", msg.chatID);
            saveUsers();
          } else {
            sendReconnectMessage("Данный пользователь уже есть в системе!", msg.chatID);
          }
        } else if (command.startsWith("/GradeUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role < 2) {
              sendReconnectMessage("Пользователя с таким йд " + userId + " уже администратор", msg.chatID);
            } else {
              user->role = 1;
              sendReconnectMessage("Вас повысил " + msg.username + " в правах, вы теперь администратор!", user->userID);
              sendReconnectMessage("Повышение пользователя " + user->userID + " успешно завершено!", msg.chatID);
              //users.put(user.userID, user);
              saveUsers();
            }

          } else {
            sendReconnectMessage("Пользователя с таким йд " + userId + " не найдено", msg.chatID);
          }
        } else if (command.startsWith("/DownGradeUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role == 2) {
              sendReconnectMessage("Пользователя с таким йд " + userId + " уже пользователь", msg.chatID);
            } else {
              user->role = 2;
              sendReconnectMessage("Вас понизил " + msg.username + " в правах, вы теперь пользователь!", user->userID);
              sendReconnectMessage("Понижение пользователя " + user->userID + " успешно завершено!", msg.chatID);
              //users.put(user.userID, user);
              saveUsers();
            }
          } else {
            sendReconnectMessage("Пользователя с таким йд " + userId + " не найдено", msg.chatID);
          }
        } else if (command.startsWith("/RemoveUser")) {
          String userId = getValue(command, '_', 1);
          if (users.containsKey(userId)) {
            User* user = users.get(userId);
            if (user->role == 0) {
              sendReconnectMessage("Нельзя удалять главного администратора", msg.chatID);
            } else {
              sendReconnectMessage("Вас удалил " + msg.username + " из системы!", user->userID);
              sendReconnectMessage("Удаление пользователя " + user->userID + " успешно завершено!", msg.chatID);
              users.remove(user->userID);
            }
          } else {
            sendReconnectMessage("Пользователя с таким йд " + userId + " не найдено", msg.chatID);
          }
        }
      }
      if (command == "/Spillage") {
        spillage();
      } else if (command.startsWith("/BordersSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        sendReconnectMessage(("Введите % порога срабатывания клапана № " + String((ind + 1)) + ":"), msg.userID);
        actionSet(msg.userID, 1400 + ind);
      } else if (command.startsWith("/NamingsSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        sendReconnectMessage(("Введите название датчика № " + String((ind + 1)) + ":"), msg.userID);
        actionSet(msg.userID, 1200 + ind);
      } else if (command.startsWith("/OperationModeSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        sendReconnectMessage(F("Выберите режим работы!"), msg.userID);
        bot.showMenuText("<Режим>", "ВКЛ. \t ВЫКЛ. \t АВТО \t А.П.", msg.userID, true);
        actionSet(msg.userID, 1300 + ind);
      } else if (command == "/AllOperationModeSet") {
        sendReconnectMessage(F("Выберите режим работы!"), msg.userID);
        bot.showMenuText("<Режим>", "ВКЛ. \t ВЫКЛ. \t АВТО \t А.П.", msg.userID, true);
        actionSet(msg.userID, 1300 + 99);
      } else if (command == "/GradeMeUp") {
        SimpleVector<String> keys = users.keys();
        for (const String& key : keys) {
          User* user = users.get(key);
          if (user->role < 2) {
            sendReconnectMessage("Пользователь: " + msg.username + " йд: " + msg.userID + ". Просит поднять его в правах. /GradeUser_" + msg.userID, user->userID);
          }
        }
        sendReconnectMessage("Ваша регистрация принята, ожидайте ответа от Администратора", msg.chatID);
      } else if (command == "/start") {
        if (check_user->role < 2) {
          String menu = F(" Перезагрузка \n Пользователи \n Управление \n Статус \n Отчеты \n Настройка ");
          String cback = F("/Restart,/Users,/control,/status,/reports,/Configure");
          bot.inlineMenuCallback("<Запуск>", menu, cback, msg.userID);
        } else {
          String menu = F(" Управление \n Статус \n Отчеты ");
          String cback = F("/control,/status,/reports");
          bot.inlineMenuCallback("<Запуск>", menu, cback, msg.userID);
        }
      } else if (command == "/Namings") {
        String menu = " Датчик влажности № 1 (" + String(myConfig.chanel[0].title)
                      + ") \n Датчик влажности № 2 (" + String(myConfig.chanel[1].title)
                      + ") \n Датчик влажности № 3 (" + String(myConfig.chanel[2].title)
                      + ") \n Датчик влажности № 4 (" + String(myConfig.chanel[3].title)
                      + ") \n Датчик влажности № 5 (" + String(myConfig.chanel[4].title)
                      + ") \n Датчик влажности № 6 (" + String(myConfig.chanel[5].title)
                      + ") \n Датчик влажности № 7 (" + String(myConfig.chanel[6].title)
                      + ") \n Датчик влажности № 8 (" + String(myConfig.chanel[7].title) + ") \n Назад ";
        String cback = F("/NamingsSet_0,/NamingsSet_1,/NamingsSet_2,/NamingsSet_3,/NamingsSet_4,/NamingsSet_5,/NamingsSet_6,/NamingsSet_7,/control");
        bot.inlineMenuCallback("<Именование>", menu, cback, msg.userID);
      } else if (command == "/Borders") {
        String menu = " Клапан № 1 (" + String(myConfig.chanel[0].title)
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
                      + " %>  \n Назад ";
        String cback = F("/BordersSet_0,/BordersSet_1,/BordersSet_2,/BordersSet_3,/BordersSet_4,/BordersSet_5,/BordersSet_6,/BordersSet_7,/control");
        bot.inlineMenuCallback("<Режим работы>", menu, cback, msg.userID);
      } else if (command == "/OperationMode") {
        String menu = " Клапан № 1 (" + String(myConfig.chanel[0].title)
                      + ") [" + String(myConfig.chanel[0].mode == 0 ? "-" : myConfig.chanel[0].mode == 1 ? "x"
                                                                          : myConfig.chanel[0].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Клапан № 2 (" + String(myConfig.chanel[1].title)
                      + ") [" + String(myConfig.chanel[1].mode == 0 ? "-" : myConfig.chanel[1].mode == 1 ? "x"
                                                                          : myConfig.chanel[1].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Клапан № 3 (" + String(myConfig.chanel[2].title)
                      + ") [" + String(myConfig.chanel[2].mode == 0 ? "-" : myConfig.chanel[2].mode == 1 ? "x"
                                                                          : myConfig.chanel[2].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Клапан № 4 (" + String(myConfig.chanel[3].title)
                      + ") [" + String(myConfig.chanel[3].mode == 0 ? "-" : myConfig.chanel[3].mode == 1 ? "x"
                                                                          : myConfig.chanel[3].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Клапан № 5 (" + String(myConfig.chanel[4].title)
                      + ") [" + String(myConfig.chanel[4].mode == 0 ? "-" : myConfig.chanel[4].mode == 1 ? "x"
                                                                          : myConfig.chanel[4].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Клапан № 6 (" + String(myConfig.chanel[5].title)
                      + ") [" + String(myConfig.chanel[5].mode == 0 ? "-" : myConfig.chanel[5].mode == 1 ? "x"
                                                                          : myConfig.chanel[5].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Клапан № 7 (" + String(myConfig.chanel[6].title)
                      + ") [" + String(myConfig.chanel[6].mode == 0 ? "-" : myConfig.chanel[6].mode == 1 ? "x"
                                                                          : myConfig.chanel[6].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Клапан № 8 (" + String(myConfig.chanel[7].title)
                      + ") [" + String(myConfig.chanel[7].mode == 0 ? "-" : myConfig.chanel[7].mode == 1 ? "x"
                                                                          : myConfig.chanel[7].mode == 2 ? "o"
                                                                                                         : "v")
                      + "] \n Установить для всех \n Назад ";
        String cback = F("/OperationModeSet_0,/OperationModeSet_1,/OperationModeSet_2,/OperationModeSet_3,/OperationModeSet_4,/OperationModeSet_5,/OperationModeSet_6,/OperationModeSet_7,/AllOperationModeSet,/control");
        bot.inlineMenuCallback("<Режим работы>", menu, cback, msg.userID);
      } else if (command == "/status") {
        hs.setAll();
        String status = String(nightNow ? "Сейчас ночь" : "Сейчас день");
        status = status + String(rainNow ? ", идёт дождь" : ", дождя нет");
        status = status + String("\n");
        status = status + String("\n") + String("Информация по датчикам");
        for (int i = 0; i < 8; i++) {

          status = status + String("\n");
          status = status + String("\n") + String("Канал № ") + String((i + 1)) + String(" (") + String(myConfig.chanel[i].title) + String(")");
          if (check_user->role == 0) {
            status = status + String("\n") + String("Текущее значение: ") + String(hs.getCurrent(i));
          }
          status = status + String("\n") + String("Текущая влажность: ") + String(hs.Percent(i)) + String(" %");
          status = status + String("\n") + String("Граничное значение: ") + String(myConfig.chanel[i].border) + String(" %");
          status = status + String("\n") + String("Клапан: ") + String((oldMode[i] == 11 || oldMode[i] == 2) ? "закрыт" : oldMode[i] == 3 ? "без контроля"
                                                                                                                                          : "открыт");
          status = status + String("\n") + String("Режим: ") + String(myConfig.chanel[i].mode == 0 ? "автоматический" : myConfig.chanel[i].mode == 1 ? "постоянно открыт"
                                                                                                                      : myConfig.chanel[i].mode == 2 ? "постоянно закрыт"
                                                                                                                                                     : "автоматический (парник)");
        }
        Serial.println(ESP.getFreeHeap());
        int mem = ESP.getFreeHeap() / 1024;
        status = status + String("\n");
        status = status + String("\n") + "Оставшаяся память : " + String(mem) + " Kb";
        sendReconnectMessage(status, msg.userID);
      } else if (command == "/Searching") {
        sendReconnectMessage(F("Произойдет поиск датчика. Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\nЗапустить поиск датчика?"), msg.userID);
        bot.showMenuText("<Поиск>", "СТАРТ \t ОТМЕНА", msg.userID, true);
        actionSet(msg.userID, 3000);
      } else if (command == "/control") {
        String menu = F(" Режим работы \n Названия \n Пороги срабатывания \n Пролив дренажа \n Поиск датчика \n Назад ");
        String cback = F("/OperationMode,/Namings,/Borders,/Spillage,/Searching,/start");
        bot.inlineMenuCallback("<Управление>", menu, cback, msg.userID);
      } else if (command == "/pause") {
        check_user->messages = false;
        sendReconnectMessage(F("Отключены все статусные сообщения"), msg.userID);
      } else if (command == "/continue") {
        check_user->messages = true;
        sendReconnectMessage(F("Статусные сообщения активированы"), msg.userID);
      } else if (command == "/GraphicsYesterday") {
        FB_Time t(getUnixTime() - 60 * 60 * 24, 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          fileToGraf(fn, msg.userID);
        } else {
          sendReconnectMessage(F("Файл за вчера не найден"), msg.userID);
        }
        command = "/Graphics";
      } else if (command == "/GraphicsToday") {
        FB_Time t(getUnixTime(), 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          fileToGraf(fn, msg.userID);
        } else {
          sendReconnectMessage(F("Файл за сегодня не найден"), msg.userID);
        }
        command = "/Graphics";
      } else if (command == "/GraphicsPeriod") {
        sendReconnectMessage(F("Введите число дней за которые хотите отобразить график"), msg.userID);
        actionSet(msg.userID, 5200);
      } else if (command == "/GraphicsTo") {
        sendReconnectMessage(F("Введите дату в формате dd.mm.yyyy за которую хотите отобразить график"), msg.userID);
        actionSet(msg.userID, 5100);
      } else if (command == "/FileTo") {
        sendReconnectMessage(F("Введите дату в формате dd.mm.yyyy за которую хотите получить файл"), msg.userID);
        actionSet(msg.userID, 5000);
      } else if (command == "/FileToday") {
        FB_Time t(getUnixTime(), 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            Serial.println("can not read file");
            sendReconnectMessage(F("Файл не открывается"), msg.userID);
          } else {
            sendReconnectMessage(F("Файл открывается, ждите ..."), msg.userID);
            fn.replace("/", "_");
            bot.sendFile(file, FB_DOC, fn, msg.chatID);
          }
          file.close();
        } else {
          sendReconnectMessage(F("Файл за сегодня не найден"), msg.userID);
          command = "/reports";
        }
      } else if (command == "/FileYesterday") {
        FB_Time t(getUnixTime() - 60 * 60 * 24, 0);
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            Serial.println("can not read file");
            sendReconnectMessage(F("Файл не открывается"), msg.userID);
          } else {
            sendReconnectMessage(F("Файл открывается, ждите ..."), msg.userID);
            fn.replace("/", "_");
            bot.sendFile(file, FB_DOC, fn, msg.chatID);
          }
          file.close();
        } else {
          sendReconnectMessage(F("Файл за вчера не найден"), msg.userID);
          command = "/reports";
        }
      } else if (command == "/GraphicsDecade") {
        fileToGrafPeriod(10, msg.userID);
        command = "/Graphics";
      }
      if (command == "/reports") {
        String menu = F(" Графики \n  Файл за вчера \n Файл текущий \n Файл... \n Назад");
        String cback = F("/Graphics,/FileYesterday,/FileToday,/FileTo,/start");
        bot.inlineMenuCallback("<Отчеты>", menu, cback, msg.userID);
      }
      if (command == "/Graphics") {
        String menu = F(" График за вчера \n График за сегодня \n График за декаду \n График за период \n График... \n Назад");
        String cback = F("/GraphicsYesterday,/GraphicsToday,/GraphicsDecade,/GraphicsPeriod,/GraphicsTo,/reports");
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
          sendReconnectMessage("Пользователь: " + msg.username + " йд: " + msg.userID + ". Просит его зарегистрировать. /AddUser_" + msg.userID, user->userID);
        }
      }
      sendReconnectMessage("Ваша регистрация принята, ожидайте ответа от Администратора", msg.chatID);
    } else {
      bot.replyMessage("Только зарегистрированные пользователи могут общаться со мной, если хотите зарегистрироваться пришлите запрос на регистрацию /register", msg.messageID, msg.chatID);
    }
  }
}

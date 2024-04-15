#include "telegram.h"
#include "objects.h"
#include "hashtable.h"
#include <EEPROM.h>
#include <FastBot.h>



void newMsg(FB_msg& msg);
void loadUsers();


void botInit() {
  bot.attach(newMsg);
  loadUsers();
  if (bot.timeSynced()) {
    FB_Time t = bot.getTime(3);
    Serial.println("Time Date");
    Serial.print(t.timeString());  // ЧЧ:ММ:СС
    Serial.print(' ');
    Serial.println(t.dateString());  // ДД.ММ.ГГГГ
    DateTime now = rtc.now();

    int64_t t1 = now.unixtime();
    int64_t t2 = bot.getUnix() + 3600 * 3;

    if (abs(t1 - t2) > 9) {
      rtc.adjust(DateTime(t.year, t.month, t.day, t.hour, t.minute, t.second));
      Serial.println("RTS Adjusting");
    }
  }
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

  User(const String& u, byte r)
    : userID(u), role(r) {}
};

struct SaveUser {
  char userID[12] = "";
  byte role = 0;
};


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
  if (bot.timeSynced()) {
    FB_Time t = bot.getTime(3);
    Serial.println("Time Date");
    Serial.print(t.timeString());  // ЧЧ:ММ:СС
    Serial.print(' ');
    Serial.println(t.dateString());  // ДД.ММ.ГГГГ

    DateTime now = rtc.now();

    int64_t t1 = now.unixtime();
    int64_t t2 = bot.getUnix() + 3600 * 3;

    if (abs(t1 - t2) > 9) {
      rtc.adjust(DateTime(t.year, t.month, t.day, t.hour, t.minute, t.second));
      Serial.println("RTS Adjusting");
    }
  }
}

void dropCDCard() {
  Serial.println("try send disconnect CD CARD message");
  SimpleVector<String> keys = users.keys();
  for (const String& key : keys) {
    User* user = users.get(key);
    Serial.print("Send message for user: ");
    Serial.println(user->userID);
    bot.sendMessage(F("Отсутсвует CD карта система не сможет работать!"), user->userID);
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
    }
  }
  EEPROM.end();
}

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
      if (command == "/reset") {
        act->action = 0;
      } else if (act->action >= 1130 && act->action <= 1137) {
        int ind = act->action - 1130;
        if (msg.text == "ЗАВЕРШИТЬ") {
          if (abs(hs.getHigh(ind) - hs.getLow(ind)) < 100) {
            bot.sendMessage("Ошибка калибровки датчик № " + String(ind + 1) + " слишком малое значение!\nОтменяем...", msg.userID);
            hs.setLowHighValue(ind, myConfig.calibr[ind].minVal, myConfig.calibr[ind].maxVal);
            command = F("/Calibrate");
          }
          bot.sendMessage("Калибровка завершена датчик № " + String(ind + 1) + " полностью функционален!", msg.userID);
          myConfig.calibr[ind].maxVal = hs.getHigh(ind);
          myConfig.calibr[ind].minVal = hs.getLow(ind);
          data.update();
          act->action = 0;
          command = F("/Calibrate");
        } else if (msg.text == "ОТМЕНА") {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          hs.setLowHighValue(ind, myConfig.calibr[ind].minVal, myConfig.calibr[ind].maxVal);
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
          bot.sendMessage("Установите датчик № " + String(ind + 1) + " в почву и нажмите завершить!", msg.userID);
          bot.showMenuText("<Колиброка>", "ЗАВЕРШИТЬ \t ОТМЕНА", msg.userID, true);
          actions.put(msg.userID, Action(msg.userID, 1130 + ind));
          return;
        } else if (msg.text == "ОТМЕНА") {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          hs.setLowHighValue(ind, myConfig.calibr[ind].minVal, myConfig.calibr[ind].maxVal);
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
          actions.put(msg.userID, Action(msg.userID, 1120 + ind));
          bot.sendMessage("Достаньте датчик № " + String(ind + 1) + " из воды протрите и нажмите далее!", msg.userID);
          bot.showMenuText("<Колиброка>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          return;
        } else if (msg.text == "ОТМЕНА") {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          hs.setLowHighValue(ind, myConfig.calibr[ind].minVal, myConfig.calibr[ind].maxVal);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action >= 1100 && act->action <= 1107) {
        if (msg.text == "СТАРТ") {
          int ind = act->action - 1100;
          bot.sendMessage("Положите датчик № " + String(ind + 1) + " в воду и нажмите далее!", msg.userID);
          bot.showMenuText("<Колиброка>", "ДАЛЕЕ \t ОТМЕНА", msg.userID, true);
          actions.put(msg.userID, Action(msg.userID, 1110 + ind));
          return;
        } else if (msg.text == "ОТМЕНА") {
          bot.sendMessage(F("Калибровка отменена!"), msg.userID);
          command = F("/Calibrate");
        }
        act->action = 0;
      } else if (act->action == 1) {
        act->action = 0;
        if (msg.text == "ДА") {
          res = 1;
          bot.sendMessage(F("Перезагрузка начата!"), msg.userID);
          return;
        } else if (msg.text == "НЕТ") {
          bot.sendMessage(F("Перезагрузка отменена!"), msg.userID);
          return;
        }
      } else if (act->action == 1001) {
        act->action = 0;
        if (msg.text == "ДА") {
          myConfig.runOnNight = true;
          bot.sendMessage(F("Работа ночью включена!"), msg.userID);
        } else if (msg.text == "НЕТ") {
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
        } else if (msg.text == "НЕТ") {
          myConfig.runOnRain = false;
          bot.sendMessage(F("Работа под дождём выключена!"), msg.userID);
        }
        command = "/Configure";
        data.update();
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
            myConfig.deltaCalibr = num;
            data.update();
            hs.setBorder(myConfig.deltaCalibr);
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
      if (msg.OTA) bot.sendMessage("Только владельци системы могут отправлять обновления устройства", msg.chatID);
    }

    if (command[0] == '/') {
      if (check_user->role == 0) {
      }
      if (check_user->role < 2) {
        if (command.startsWith("/HumidityCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          bot.sendMessage("Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\nЗапусить калибровку датчика № " + String(ind + 1) + "?", msg.userID);
          bot.showMenuText("<Колиброка>", "СТАРТ \t ОТМЕНА", msg.userID, true);
          actions.put(msg.userID, Action(msg.userID, 1100 + ind));
        } else if (command == "/Calibrate") {
          String menu = F("Датчик влажности № 1 \n Датчик влажности № 2 \n Датчик влажности № 3 \n Датчик влажности № 4 \n Датчик влажности № 5 \n Датчик влажности № 6 \n Датчик влажности № 7 \n Датчик влажности № 8 \n Назад");
          String cback = F("/HumidityCalibrate_0,/HumidityCalibrate_1,/HumidityCalibrate_2,/HumidityCalibrate_3,/HumidityCalibrate_4,/HumidityCalibrate_5,/HumidityCalibrate_6,/HumidityCalibrate_7,/Configure");
          bot.inlineMenuCallback("<Калибровка>", menu, cback, msg.userID);
        } else if (command == "/DeltaCalibr") {
          bot.sendMessage(F("Введите значение (от 0 до 2048):"), msg.userID);
          actions.put(msg.userID, Action(msg.userID, 1004));
        } else if (command == "/DeltaHumidity") {
          bot.sendMessage(F("Введите значение (от 0 до 100) %:"), msg.userID);
          actions.put(msg.userID, Action(msg.userID, 1003));
        } else if (command == "/WorkAtRain") {
          bot.sendMessage(F("Включить режим работы во время дождя!"), msg.userID);
          bot.showMenuText("<Включить>", "ДА \t НЕТ", msg.userID, true);
          actions.put(msg.userID, Action(msg.userID, 1002));
        } else if (command == "/WorkAtNight") {
          bot.sendMessage(F("Включить режим работы в ночное время!"), msg.userID);
          bot.showMenuText("<Включить>", "ДА \t НЕТ", msg.userID, true);
          actions.put(msg.userID, Action(msg.userID, 1001));
        } else if (command == "/Configure") {
          ///TODO сделать конфигурацию в зависимости от датчиков
          String menu = ("Работа ночью " + String(myConfig.runOnNight ? "[x]" : "[ ]") + " \n Работа под дождём " + String(myConfig.runOnRain ? "[x]" : "[ ]") + " \n Дельта влажности % (" + String(myConfig.deltaHum) + ") \n Дельта калибровки (" + String(myConfig.deltaCalibr) + ") \n Калибровка \n Назад");
          String cback = F("/WorkAtNight,/WorkAtRain,/DeltaHumidity,/DeltaCalibr,/Calibrate,/reset");
          bot.inlineMenuCallback("<Настройка>", menu, cback, msg.userID);
        } else if (command == "/Restart") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {
            User* user = users.get(key);
            bot.sendMessage("Система будет перзагруженна!", user->userID);
          }
          // res = 1;
          bot.showMenuText("<Перезагрузка>", "ДА \t НЕТ", msg.chatID, true);
          actions.put(msg.userID, Action(msg.userID, 1));
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
              bot.sendMessage("Вас повысил " + msg.username + " в провах, вы теперь админимтратор!", user->userID);
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
              bot.sendMessage("Вас понизил " + msg.username + " в провах, вы теперь пользователь!", user->userID);
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
      if (command == "/GradeMeUp") {
        SimpleVector<String> keys = users.keys();
        for (const String& key : keys) {
          User* user = users.get(key);
          if (user->role < 2) {
            bot.sendMessage("Пользователь: " + msg.username + " йд: " + msg.userID + ". Просит поднять его в провах. /GradeUser_" + msg.userID, user->userID);
          }
        }
        bot.sendMessage("Ваша регистрация принята, ожидайте ответа от Администратора", msg.chatID);
      } else if (command == "/reset") {
        if (check_user->role < 2) {
          String menu = F("Перезагрузка \n Пользователи \n Управление \n Статус \n Настройка");
          String cback = F("/Restart,/Users,/control,/status,/Configure");
          bot.inlineMenuCallback("<Запуск>", menu, cback, msg.userID);
        }
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

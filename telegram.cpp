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
  }
}
class Action {
public:
  String userID;
  byte action;

  Action(const String& u, byte a)
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
      bot.replyMessage("Привет, первый администратор!", msg.messageID, msg.chatID);
      users.put(msg.userID, User(msg.userID, 0));
      saveUsers();
    } else {
      bot.replyMessage("Первый запрос должен быть с ключевым словом, полученным при настройке Wifi", msg.messageID, msg.chatID);
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
      if (act->action == 1) {
        act->action = 0;
        if (msg.text == "ДА") {
          res = 1;
          bot.sendMessage("Перезагрузка начата!", msg.userID);
          return;
        } else if (msg.text == "НЕТ") {
          bot.sendMessage("Перезагрузка отменена!", msg.userID);
          return;
        }
      }
    }

    if (msg.OTA && check_user->role < 2 && msg.fileName == "update.bin") {
      bot.update();
      return;
    }

    if (command[0] == '/') {
      if (check_user->role == 0) {
      }
      if (check_user->role < 2) {
        if (command == "/Restart") {
          SimpleVector<String> keys = users.keys();
          for (const String& key : keys) {  // Iterate through the keys, using a range-based for loop... String& is used to avoid copying the key
            User* user = users.get(key);    // Get the Person object associated with the key
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
          for (const String& key : keys) {  // Iterate through the keys, using a range-based for loop... String& is used to avoid copying the key
            User* user = users.get(key);    // Get the Person object associated with the key
            bot.sendMessage("Пользователь йд: " + user->userID + ". Роль: " + user->role, msg.chatID);
          }
          //if (msg.query == 1) bot.answer("Выполнено", FB_NOTIF);
        } else if (command == "/UsersDownEdit") {
          SimpleVector<String> keys = users.keys();
          String menu = "";
          String cback = "";
          for (const String& key : keys) {  // Iterate through the keys, using a range-based for loop... String& is used to avoid copying the key
            User* user = users.get(key);    // Get the Person object associated with the key
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
          for (const String& key : keys) {  // Iterate through the keys, using a range-based for loop... String& is used to avoid copying the key
            User* user = users.get(key);    // Get the Person object associated with the key
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
          for (const String& key : keys) {  // Iterate through the keys, using a range-based for loop... String& is used to avoid copying the key
            User* user = users.get(key);    // Get the Person object associated with the key
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
        for (const String& key : keys) {  // Iterate through the keys, using a range-based for loop... String& is used to avoid copying the key
          User* user = users.get(key);    // Get the Person object associated with the key
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
      for (const String& key : keys) {  // Iterate through the keys, using a range-based for loop... String& is used to avoid copying the key
        User* user = users.get(key);    // Get the Person object associated with the key
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

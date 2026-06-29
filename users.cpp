// users.cpp 👥 Реализация модели пользователей и хранения в EEPROM
#include "users.h"
#include <EEPROM.h>
#include "log.h"

// 🗃️ Пользователи — фиксированный массив (без динамической памяти/фрагментации кучи)
User    users[MAX_USERS];
uint8_t userCount = 0;

// 💾 EEPROM-представление пользователя (POD фиксированного размера)
// Раскладка в EEPROM: [250] = int count, [255 + i*sizeof(SaveUser)] = записи.
struct SaveUser {
  char userID[12] = "";
  byte role = 0;
  bool messages = true;  // 📨 сохраняем флаг подписки на сообщения
};

// 🔍 Поиск пользователя по ID
User* findUser(const String& id) {
  for (uint8_t i = 0; i < userCount; i++)
    if (users[i].userID == id) return &users[i];
  return nullptr;
}

// ➕ Добавить пользователя
bool addUser(const String& id, byte role) {
  if (findUser(id) != nullptr) return false;
  if (userCount >= MAX_USERS) {
    LOG_W("Достигнут лимит пользователей (%d) — %s не добавлен", MAX_USERS, id.c_str());
    return false;
  }
  users[userCount] = User(id, role);
  userCount++;
  LOG_I("Добавлен пользователь %s (роль %d), всего %d", id.c_str(), role, userCount);
  return true;
}

// 🗑️ Удалить пользователя по ID (сдвигаем хвост, очищаем последний слот)
void removeUser(const String& id) {
  for (uint8_t i = 0; i < userCount; i++) {
    if (users[i].userID == id) {
      for (uint8_t j = i; j + 1 < userCount; j++) users[j] = users[j + 1];
      userCount--;
      users[userCount] = User();
      LOG_I("Удалён пользователь %s, осталось %d", id.c_str(), userCount);
      return;
    }
  }
}

// 💾 Сохранение списка пользователей в EEPROM
void saveUsers() {
  LOG_D("Запись пользователей в EEPROM");
  EEPROM.begin(4096);
  int count = userCount;
  EEPROM.put(250, count);
  LOG_I("Сохранено пользователей: %d", count);
  for (uint8_t ind = 0; ind < userCount; ind++) {
    SaveUser usr;
    strncpy(usr.userID, users[ind].userID.c_str(), sizeof(usr.userID) - 1);
    usr.userID[sizeof(usr.userID) - 1] = '\0';
    usr.role = users[ind].role;
    usr.messages = users[ind].messages;
    int shift = 255 + ind * sizeof(SaveUser);
    EEPROM.put(shift, usr);
    LOG_D("  user #%d id=%s role=%d (EEPROM@%d)", ind + 1, usr.userID, usr.role, shift);
  }
  EEPROM.commit();
  EEPROM.end();
}

// 📥 Загрузка списка пользователей из EEPROM в массив (только данные)
void loadUsersData() {
  EEPROM.begin(4096);
  int ind = 0;
  EEPROM.get(250, ind);
  LOG_D("Чтение пользователей из EEPROM");
  LOG_I("Загружено пользователей: %d", ind);
  if (ind > MAX_USERS) ind = MAX_USERS;  // 🛡️ не выходим за размер массива
  if (ind < 0) ind = 0;
  userCount = 0;
  for (int i = 0; i < ind; i++) {
    SaveUser usr;
    int shift = 255 + i * sizeof(SaveUser);
    EEPROM.get(shift, usr);
    users[userCount].userID = String(usr.userID);
    users[userCount].role = usr.role;
    users[userCount].messages = usr.messages;
    userCount++;
    LOG_D("  user id=%s role=%d", usr.userID, usr.role);
  }
  EEPROM.end();
}

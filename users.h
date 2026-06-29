// users.h 👥 Модель пользователей Telegram и их хранение (без зависимости от бота)
// Данные + персистентность в EEPROM. Презентация (стартовые меню, рассылки)
// остаётся на стороне telegram.cpp.
#pragma once

#include <Arduino.h>

constexpr uint8_t MAX_USERS = 8;  // 👥 максимум пользователей (фикс-массив, без кучи)

// 👤 Пользователь Telegram
class User {
public:
  String userID;          // 👤 ID пользователя
  byte role;              // 🎭 0=владелец, 1=админ, 2=пользователь
  bool messages = true;   // 📨 получать ли статусные сообщения
  int action = 0;         // 🎬 состояние диалога (FSM, RAM-only)

  User() : userID(""), role(2), messages(true) {}
  User(const String& u, byte r) : userID(u), role(r), messages(true) {}
};

extern User    users[MAX_USERS];
extern uint8_t userCount;

// 🔍 Поиск пользователя по ID (nullptr, если не найден)
User* findUser(const String& id);
// ➕ Добавить (false — уже есть или нет свободного слота)
bool  addUser(const String& id, byte role);
// 🗑️ Удалить по ID (сдвигает хвост)
void  removeUser(const String& id);

// 💾 Сохранить список пользователей в EEPROM
void  saveUsers();
// 📥 Загрузить список из EEPROM в массив (без сайд-эффектов — только данные)
void  loadUsersData();

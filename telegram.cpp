// telegram.cpp 🤖💬 Модуль Telegram бота на базе FastBot2
// 🌱💧 Система капельного полива с 8 каналами датчиков влажности
#include "telegram.h"
#include "objects.h"
#include "users.h"         // 👥 модель пользователей и их хранение
#include "botutil.h"       // 🧰 isNumeric/getValue/IntWith2Zero
#include "bot_transport.h" // 📡 sendReconnectMessage
#include "reports.h"       // 📊 fileToGraf/fileToGrafPeriod/rm
#include "log.h"
#include <EEPROM.h>
#include "valves.h"

// ============================================================
// 🎬 Коды состояний диалога (FSM). Диапазоны = база + индекс канала (0..Dlg::Last).
// ============================================================
namespace Dlg {
  constexpr int None          = 0;     // нет активного диалога

  constexpr int Restart       = 1;     // подтверждение перезагрузки
  constexpr int WorkNight     = 1001;  // вкл/выкл работу ночью
  constexpr int WorkRain      = 1002;  // вкл/выкл работу под дождём
  constexpr int DeltaHum      = 1003;  // ввод дельты влажности
  constexpr int DeltaCalib    = 1004;  // ввод дельты калибровки
  constexpr int DelFolder     = 1005;  // ввод года для удаления
  constexpr int BoostPump     = 1006;  // ввод порога насоса давления
  constexpr int ClogThreshold = 1007;  // ввод порога контроля фильтра
  constexpr int TgTimeout     = 1008;  // ввод таймаута связи с Telegram

  // Калибровка датчика (база + индекс канала)
  constexpr int CalibStart    = 1100;  // старт
  constexpr int CalibWet      = 1110;  // замер «вода»
  constexpr int CalibDry      = 1120;  // замер «сухо»
  constexpr int CalibFinish   = 1130;  // завершение

  constexpr int Rename        = 1200;  // переименование датчика (база + канал)
  constexpr int ModeSet       = 1300;  // режим клапана (база + канал)
  constexpr int ModeSetAll    = 1399;  // режим всех клапанов
  constexpr int Border        = 1400;  // порог влажности (база + канал)
  constexpr int CalibManual   = 1800;  // ручная калибровка мин,макс (база + канал)

  constexpr int Reset         = 2000;  // сброс настроек

  constexpr int SearchStart   = 3000;  // поиск датчика: старт
  constexpr int SearchWet     = 3010;  // поиск датчика: замер «вода»
  constexpr int SearchDetect  = 3020;  // поиск датчика: определение

  constexpr int ClearFlow     = 4000;  // стереть данные расхода

  constexpr int SendFile      = 5000;  // отправка CSV по дате
  constexpr int GraphDate     = 5100;  // график за дату
  constexpr int GraphPeriod   = 5200;  // график за период

  constexpr int Last = NUM_CHANNELS - 1;  // верхняя граница диапазона: база + Last
}

// ============================================================
// 🔄 Флаг необходимости сохранения конфигурации на SD-карту
// ============================================================
bool needUpdate = false;

// 🔄 Проверить и сбросить флаг необходимости обновления конфигурации
bool telegramNeedUpdate() {
  bool nu = needUpdate;
  needUpdate = false;
  return nu;
}

// 🛡️ Индекс канала, разобранный из команды вида "/BordersSet_N" (пользовательский
// текст или callback), может быть любым числом — кнопки в меню всегда шлют 0..7,
// но ничто не мешает зарегистрированному пользователю набрать команду вручную с
// произвольным N. Без проверки такой N уходит в Dlg::Base + ind и приводит к
// выходу за границы myConfig.chanel[]/HumiditySensors (порча Config, крэш).
static bool validChannelIndex(int ind, const String& userID) {
  if (ind < 0 || ind >= NUM_CHANNELS) {
    sendReconnectMessage(F("❌ Неверный номер канала"), userID);
    return false;
  }
  return true;
}

// ============================================================
// 🎹 Показ меню с учётом контекста (callback-кнопка или текст)
// ============================================================
// По нажатию inline-кнопки редактируем существующее сообщение (меню
// «переключается» на месте); в текстовом контексте (ответ на диалог Да/Нет,
// ввод числа, команда, набранная вручную) редактировать нечего — отправляем
// новое сообщение. Раньше все меню делали editText безусловно, из-за чего
// после текстового ответа меню молча не показывалось.
static void showMenu(fb::Update& u, const String& userID, const String& text, fb::InlineKeyboard& menu) {
  if (u.isQuery()) {
    fb::TextEdit t;
    t.mode = fb::Message::Mode::HTML;
    t.text = text;
    t.chatID = u.query().message().chat().id();
    t.messageID = u.query().message().id();
    t.setKeyboard(&menu);
    bot.editText(t);
  } else {
    fb::Message m;
    m.text = text;
    m.chatID = userID;
    m.setModeHTML();
    m.setKeyboard(&menu);
    bot.sendMessage(m);
  }
}

// 🏠 Главное меню (наполнение зависит от роли; используется при старте и на /Start)
static void buildMainMenu(fb::InlineKeyboard& menu, bool admin) {
  if (admin) {
    menu.addButton("🔄 Перезагрузка", "/Restart", fb::KeyStyle::Danger).newRow()
        .addButton("👥 Пользователи", "/Users", fb::KeyStyle::Primary).newRow();
  }
  menu.addButton("⚙️ Управление", "/Control", fb::KeyStyle::Primary).newRow()
      .addButton("📊 Статус", "/status", fb::KeyStyle::Primary).newRow()
      .addButton("📈 Отчёты", "/Reports", fb::KeyStyle::Primary);
  if (admin) {
    menu.newRow().addButton("🔧 Настройка", "/Configure", fb::KeyStyle::Primary);
  }
}

// 📊 rm()/fileToGraf()/fileToGrafPeriod() вынесены в reports.cpp

// 🔮 Предварительное объявление обработчика сообщений (FastBot2 callback)
void newMsg(fb::Update& u);
void loadUsers();

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

  // 🔌 Подключаем контроль связи с Telegram (детект разрыва/восстановления)
  botMonitorAttach();

  // 📥 Загружаем зарегистрированных пользователей из EEPROM
  loadUsers();
  LOG_I("Telegram-бот запущен (long-poll 60 с)");
}

// ============================================================
// 👥 Классы для управления пользователями и действиями
// ============================================================

// 👥 Модель пользователей (User, массив, find/add/removeUser, save/loadUsersData)
//    вынесена в users.h / users.cpp. Здесь остаётся только презентация бота.

// 📡 Транспорт (sendReconnectMessage/sendStatus/dropCDCard/
//    connectCDCard) вынесён в bot_transport.cpp.

// ============================================================
// 🎬 Установить текущее действие пользователя (конечный автомат)
// ============================================================
void actionSet(String userID, int action) {
  User* u = findUser(userID);
  if (u != nullptr) u->action = action;
}

// ============================================================
// 📥 Загрузка списка пользователей из EEPROM при старте
// ============================================================
void loadUsers() {
  loadUsersData();  // 📥 данные грузим из EEPROM (модуль users)
  LOG_D("Рассылка стартового меню: %d польз.", userCount);

  // 🎹 Каждому загруженному пользователю — приветствие и меню по роли
  for (uint8_t i = 0; i < userCount; i++) {
    User* u = &users[i];
    sendReconnectMessage("🚀 Система запущена!", u->userID);

    fb::InlineKeyboard menu;
    buildMainMenu(menu, u->role < 2);

    fb::Message msg;
    msg.text = "🚀 <b>Запуск</b>";
    msg.chatID = u->userID;
    msg.setModeHTML();
    msg.setKeyboard(&menu);
    bot.sendMessage(msg);
  }
}

// ============================================================
// 🔍 Массив для операции поиска датчика (временные значения АЦП)
// ============================================================
int search[NUM_CHANNELS] = { 0 };

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
  LOG_D("Msg %s (%s): '%s' data='%s' [%s]", userID.c_str(), username.c_str(),
        text.c_str(), data.c_str(), u.isQuery() ? "query" : "message");

  // ============================================================
  // 🆕 ПЕРВИЧНАЯ РЕГИСТРАЦИЯ: если нет пользователей — первый вводит кодовое слово
  // ============================================================
  if (userCount == 0) {
    if (text == String(tstr)) {
      // ✅ Кодовое слово совпало — регистрируем как владельца (роль 0)
      fb::Message reply;
      reply.text = "👑 Привет, владелец системы!";
      reply.chatID = chatID;
      reply.reply.messageID = msg.id().toInt();  // 📎 Ответ на конкретное сообщение
      bot.sendMessage(reply);

      addUser(userID, 0);
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
  if (findUser(userID) != nullptr) {
    User* check_user = findUser(userID);
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
    {
      User* act = check_user;  // 🎬 состояние диалога хранится в самом пользователе

      // 🔙 Сброс действия при получении команды
      if (command.startsWith("/")) {
        act->action = Dlg::None;
      }
      // 🔧 Калибровка: этап 3 — завершение (1130–1137)
      else if (act->action >= Dlg::CalibFinish && act->action <= Dlg::CalibFinish + Dlg::Last) {
        int ind = act->action - Dlg::CalibFinish;
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
          act->action = Dlg::None;
          command = F("/Calibrate");
        } else {
          sendReconnectMessage(F("❌ Калибровка отменена!"), userID);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
          act->action = Dlg::None;
        }
      }
      // 🔧 Калибровка: этап 2 — сухое значение (1120–1127)
      else if (act->action >= Dlg::CalibDry && act->action <= Dlg::CalibDry + Dlg::Last) {
        int ind = act->action - Dlg::CalibDry;
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          int val = hs.setHigh(ind);
          LOG_D("Сухое значение, датчик %d: %d", ind, val);
          sendReconnectMessage("🌵 Установите датчик № " + String((ind + 1)) + " в почву и нажмите завершить!", userID);

          fb::Keyboard kb;
          kb.addButton("✅ ЗАВЕРШИТЬ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Калибровка>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          act->action = Dlg::CalibFinish + ind;
          return;
        } else {
          sendReconnectMessage(F("❌ Калибровка отменена!"), userID, true);
          hs.setLowHighValue(ind, myConfig.chanel[ind].minVal, myConfig.chanel[ind].maxVal);
          command = F("/Calibrate");
          act->action = Dlg::None;
        }
      }
      // 🔧 Калибровка: этап 1 — влажное значение (1110–1117)
      else if (act->action >= Dlg::CalibWet && act->action <= Dlg::CalibWet + Dlg::Last) {
        int ind = act->action - Dlg::CalibWet;
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          int val = hs.setLow(ind);
          LOG_D("Влажное значение, датчик %d: %d", ind, val);
          act->action = Dlg::CalibDry + ind;
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
          act->action = Dlg::None;
        }
      }
      // 🔧 Калибровка: этап 0 — старт (1100–1107)
      else if (act->action >= Dlg::CalibStart && act->action <= Dlg::CalibStart + Dlg::Last) {
        if (text == "🚀 СТАРТ") {
          int ind = act->action - Dlg::CalibStart;
          sendReconnectMessage("💧 Положите датчик № " + String((ind + 1)) + " в воду и нажмите далее!", userID);

          fb::Keyboard kb;
          kb.addButton("➡️ ДАЛЕЕ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Калибровка>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          act->action = Dlg::CalibWet + ind;
          return;
        } else {
          sendReconnectMessage(F("❌ Калибровка отменена!"), userID, true);
          command = F("/Calibrate");
          act->action = Dlg::None;
        }
      }
      // 🔍 Поиск датчика: этап 0 — старт (3000)
      else if (act->action == Dlg::SearchStart) {
        if (text == "🚀 СТАРТ") {
          sendReconnectMessage("💧 Положите датчик в воду и нажмите далее!", userID);

          fb::Keyboard kb;
          kb.addButton("➡️ ДАЛЕЕ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Поиск>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          act->action = Dlg::SearchWet;
          return;
        } else {
          sendReconnectMessage(F("❌ Поиск отменён!"), userID, true);
          command = F("/control");
          act->action = Dlg::None;
        }
      }
      // 🔍 Поиск датчика: этап 1 — влажное значение (3010)
      else if (act->action == Dlg::SearchWet) {
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          for (int i = 0; i < NUM_CHANNELS; i++) {
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

          act->action = Dlg::SearchDetect;
          return;
        } else {
          sendReconnectMessage(F("❌ Поиск отменён!"), userID, true);
          command = F("/control");
          act->action = Dlg::None;
        }
      }
      // 🔍 Поиск датчика: этап 2 — определение (3020)
      else if (act->action == Dlg::SearchDetect) {
        if (text == "➡️ ДАЛЕЕ") {
          hs.setAll();
          int ind = -1;
          for (int i = 0; i < NUM_CHANNELS; i++) {
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
        act->action = Dlg::None;
      }
      // 🎯 Установка порога влажности (1400–1407)
      else if (act->action >= Dlg::Border && act->action <= Dlg::Border + Dlg::Last) {
        int ind = act->action - Dlg::Border;
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 100) {
            act->action = Dlg::None;
            myConfig.chanel[ind].border = num;
            needUpdate = true;
            sendReconnectMessage("✅ Порог клапана № " + String(ind + 1) + " установлен: " + String(num) + " %", userID);
            command = "/Borders";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 100) % !"), userID);
            return;
          }
        }
      }
      // 📝 Переименование датчика (1200–1207)
      else if (act->action >= Dlg::Rename && act->action <= Dlg::Rename + Dlg::Last) {
        int ind = act->action - Dlg::Rename;
        // 🛡️ text — произвольный ввод из Telegram (до 4096 байт), а title — буфер
        // фиксированного размера char[90]: strcpy без ограничения переполнил бы его
        // и повредил соседние поля Config. Также убираем запятую/перевод строки
        // (ломают формат CSV-логов, reports.cpp разбирает их по запятой) и
        // < > & (title потом вставляется в HTML-режимные сообщения бота — сырые
        // угловые скобки/амперсанд ломают разбор HTML на стороне Telegram).
        String safe = text;
        safe.replace(",", " ");
        safe.replace("\n", " ");
        safe.replace("\r", " ");
        safe.replace("<", "‹");
        safe.replace(">", "›");
        safe.replace("&", "и");
        strncpy(myConfig.chanel[ind].title, safe.c_str(), sizeof(myConfig.chanel[ind].title) - 1);
        myConfig.chanel[ind].title[sizeof(myConfig.chanel[ind].title) - 1] = '\0';
        command = F("/Namings");
        act->action = Dlg::None;
        needUpdate = true;
        sendReconnectMessage("✅ Датчик № " + String(ind + 1) + " переименован: " + safe, userID);
      }
      // 🚰 Установка режима работы клапана (1300–1307)
      else if (act->action >= Dlg::ModeSet && act->action <= Dlg::ModeSet + Dlg::Last) {
        int ind = act->action - Dlg::ModeSet;
        if (text == "✅ ВКЛ.") {
          sendReconnectMessage(F("✅ Клапан включён!"), userID, true);
          myConfig.chanel[ind].mode = Mode::AlwaysOn;
          valveOpen(ind);
        } else if (text == "⛔ ВЫКЛ.") {
          sendReconnectMessage(F("⛔ Клапан выключен!"), userID, true);
          myConfig.chanel[ind].mode = Mode::AlwaysOff;
          valveClose(ind);
        } else if (text == "🤖 АВТО") {
          sendReconnectMessage(F("🤖 Клапан в автоматическом режиме!"), userID, true);
          myConfig.chanel[ind].mode = Mode::Auto;
        } else if (text == "🏠 А.П.") {
          sendReconnectMessage(F("🏠 Клапан в автоматическом режиме для парника!"), userID, true);
          myConfig.chanel[ind].mode = Mode::Greenhouse;
        }
        needUpdate = true;
        act->action = Dlg::None;
        command = F("/OperationMode");
      }
      // 🚰 Установка режима для ВСЕХ клапанов (1399)
      else if (act->action == Dlg::ModeSetAll) {
        uint8_t md = Mode::Auto;
        if (text == "✅ ВКЛ.") {
          sendReconnectMessage(F("✅ Клапаны включены!"), userID, true);
          md = Mode::AlwaysOn;
        } else if (text == "⛔ ВЫКЛ.") {
          sendReconnectMessage(F("⛔ Клапаны выключены!"), userID, true);
          md = Mode::AlwaysOff;
        } else if (text == "🤖 АВТО") {
          sendReconnectMessage(F("🤖 Клапаны в автоматическом режиме!"), userID, true);
          md = Mode::Auto;
        } else if (text == "🏠 А.П.") {
          sendReconnectMessage(F("🏠 Клапаны в автоматическом режиме для парника!"), userID, true);
          md = Mode::Greenhouse;
        }
        for (int l = 0; l < NUM_CHANNELS; l++) {
          myConfig.chanel[l].mode = md;
          if (md == Mode::AlwaysOn) {
            valveOpen(l);
          }
          if (md == Mode::AlwaysOff) {
            valveClose(l);
          }
        }
        needUpdate = true;
        act->action = Dlg::None;
        command = F("/OperationMode");
      }
      // 🔧 Ручная калибровка (1800–1807)
      else if (act->action >= Dlg::CalibManual && act->action <= Dlg::CalibManual + Dlg::Last) {
        int ind = act->action - Dlg::CalibManual;
        String minvs = getValue(text, ',', 0);
        String maxvs = getValue(text, ',', 1);

        if (isNumeric(minvs) && isNumeric(maxvs)) {
          int minv = minvs.toInt();
          int maxv = maxvs.toInt();
          if (minv >= 0 && minv <= 4096 && maxv >= 0 && maxv <= 4096) {
            act->action = Dlg::None;
            hs.setLowHighValue(ind, minv, maxv);
            command = "/CalibrateManual";
            myConfig.chanel[ind].maxVal = hs.getHigh(ind);
            myConfig.chanel[ind].minVal = hs.getLow(ind);
            needUpdate = true;
            sendReconnectMessage("✅ Калибровка датчика № " + String(ind + 1) + " сохранена (мин " + String(minv) + ", макс " + String(maxv) + ")", userID);
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
      else if (act->action == Dlg::Reset) {
        act->action = Dlg::None;
        if (text == "✅ ДА") {
          myConfig.deltaCalibration = 15;
          myConfig.deltaHum = 5;
          myConfig.runOnNight = false;
          myConfig.runOnRain = true;
          myConfig.boostPumpValves = 9;  // 💪 насос давления по умолчанию выключен
          myConfig.clogThresholdPercent = 50;  // 🧽 порог контроля фильтра
          myConfig.flowMonitorEnabled = true;  // 🧽 контроль фильтра включён
          myConfig.tgTimeoutSec = TG_TIMEOUT_DEFAULT;  // 🔌 таймаут связи Telegram
          myConfig.cleanFlowPerValve = 0.0;  // 🧽 сброс эталона фильтра (на клапан)
          myConfig.cleanFlowDrain = 0.0;     // 🧽 сброс эталона фильтра (дренаж)
          hs.setBorder(myConfig.deltaCalibration);
          for (int i = 0; i < NUM_CHANNELS; i++) {
            myConfig.chanel[i].border = 60;
            myConfig.chanel[i].maxVal = 1024;
            myConfig.chanel[i].minVal = 1024;
            myConfig.chanel[i].mode = Mode::Auto;
            strcpy(myConfig.chanel[i].title, String("🌱 Растение").c_str());
          }
          LOG_W("Сброс настроек к значениям по умолчанию (выполнил %s)", username.c_str());
          sendReconnectMessage(F("✅ Сброс выполнен!"), userID, true);
        } else {
          sendReconnectMessage(F("❌ Сброс отменён!"), userID, true);
        }
        command = "/configure";
        needUpdate = true;
      }
      // 🔄 Перезагрузка (1)
      else if (act->action == Dlg::Restart) {
        act->action = Dlg::None;
        if (text == "✅ ДА") {
          LOG_W("Запрошена перезагрузка устройства (%s)", username.c_str());
          res = 1;
          sendReconnectMessage(F("🔄 Перезагрузка начата!"), userID, true);
          return;
        } else {
          sendReconnectMessage(F("❌ Перезагрузка отменена!"), userID, true);
          return;
        }
      }
      // Стереь все данные по проливу
      else if (act->action == Dlg::ClearFlow) {
        act->action = Dlg::None;
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
      else if (act->action == Dlg::WorkNight) {
        act->action = Dlg::None;
        if (text == "✅ ДА") {
          myConfig.runOnNight = true;
          sendReconnectMessage(F("🌙 Работа ночью включена!"), userID, true);
        } else {
          myConfig.runOnNight = false;
          sendReconnectMessage(F("🌙 Работа ночью выключена!"), userID, true);
        }
        command = "/configure";
        needUpdate = true;
      }
      // 🌧️ Работа под дождём (1002)
      else if (act->action == Dlg::WorkRain) {
        act->action = Dlg::None;
        if (text == "✅ ДА") {
          myConfig.runOnRain = true;
          sendReconnectMessage(F("🌧️ Работа под дождём включена!"), userID);
        } else {
          myConfig.runOnRain = false;
          sendReconnectMessage(F("🌧️ Работа под дождём выключена!"), userID);
        }
        command = "/configure";
        needUpdate = true;
      }
      // 📁 Отправка файла по дате (5000)
      else if (act->action == Dlg::SendFile) {
        String input = String(text);
        String sd = getValue(input, '.', 0);
        String sm = getValue(input, '.', 1);
        String sy = getValue(input, '.', 2);

        String fn = String("/") + sy + String("/") + sm + String("/") + sd + String(".csv");

        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            sendReconnectMessage(F("❌ Файл не открывается"), userID);
            LOG_W("Не удалось открыть файл");
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
        act->action = Dlg::None;
      }
      // 📊 График за период (5200)
      else if (act->action == Dlg::GraphPeriod) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 1 && num <= 60) {
            act->action = Dlg::None;
            fileToGrafPeriod(num, userID);
            command = "/Graphics";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 60)!"), userID);
            return;
          }
        } else {
          LOG_D("Некорректный ввод пользователя");
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
      // 📊 График за конкретную дату (5100)
      else if (act->action == Dlg::GraphDate) {
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
        act->action = Dlg::None;
        command = "/Graphics";
      }
      // 🗑️ Удаление папки года (1005)
      else if (act->action == Dlg::DelFolder) {
        String input = String(text);
        if (isNumeric(input)) {
          Datime t = getDateTime();
          if (t.year == input.toInt()) {
            LOG_D("Некорректный ввод пользователя");
            sendReconnectMessage(F("❌ Удалять текущий год запрещено!"), userID);
            return;
          }
          String del = "/" + input;
          if (!SD.exists(del)) {
            LOG_D("Некорректный ввод пользователя");
            sendReconnectMessage(F("❌ Записей запрашиваемого года не найдено!"), userID);
            act->action = Dlg::None;
            command = "/configure";
          } else {
            File dir = SD.open(del);
            sendReconnectMessage(F("🗑️ Началось удаление файлов, ожидайте..."), userID);
            rm(dir, del + "/");
            dir.close();
            SD.rmdir(del);
            command = "/configure";
            act->action = Dlg::None;
          }
        } else {
          LOG_D("Некорректный ввод пользователя");
          sendReconnectMessage(F("❌ Ожидался ввод года!"), userID);
          return;
        }
      }
      // 💧 Дельта влажности (1003)
      else if (act->action == Dlg::DeltaHum) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 100) {
            act->action = Dlg::None;
            myConfig.deltaHum = num;
            needUpdate = true;
            sendReconnectMessage("✅ Дельта влажности: " + String(num) + " %", userID);
            command = "/configure";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 100) % !"), userID);
            return;
          }
        } else {
          LOG_D("Некорректный ввод пользователя");
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
      // 🔧 Дельта калибровки (1004)
      else if (act->action == Dlg::DeltaCalib) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 0 && num <= 2048) {
            act->action = Dlg::None;
            myConfig.deltaCalibration = num;
            needUpdate = true;
            hs.setBorder(myConfig.deltaCalibration);
            sendReconnectMessage("✅ Дельта калибровки: " + String(num), userID);
            command = "/configure";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение (от 0 до 2048)!"), userID);
            return;
          }
        } else {
          LOG_D("Некорректный ввод пользователя");
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
      // 💪 Насос повышения давления (1006): порог открытых клапанов 1..8, 9 = выкл
      else if (act->action == Dlg::BoostPump) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 1 && num <= 9) {
            act->action = Dlg::None;
            myConfig.boostPumpValves = num;
            needUpdate = true;
            stopPumpIfNeed();  // 💪 применяем порог сразу (вкл/выкл без переоткрытия клапана)
            sendReconnectMessage(num >= 9 ? String("✅ Насос давления: выключен")
                                          : "✅ Насос давления: включать при ≥ " + String(num) + " клапанах", userID);
            command = "/configure";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение от 1 до 9!"), userID);
            return;
          }
        } else {
          LOG_D("Некорректный ввод пользователя");
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
      // 🧽 Порог тревоги засора фильтра (1007): 10..90 %
      else if (act->action == Dlg::ClogThreshold) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 10 && num <= 90) {
            act->action = Dlg::None;
            myConfig.clogThresholdPercent = num;
            needUpdate = true;
            sendReconnectMessage("✅ Контроль фильтра: тревога при потоке ниже " + String(num) + " % от нормы", userID);
            command = "/configure";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение от 10 до 90 %!"), userID);
            return;
          }
        } else {
          LOG_D("Некорректный ввод пользователя");
          sendReconnectMessage(F("❌ Ожидался ввод целого числа — повторите!"), userID);
          return;
        }
      }
      // 🔌 Таймаут связи с Telegram (1008): 120..3600 с
      else if (act->action == Dlg::TgTimeout) {
        String input = String(text);
        if (isNumeric(input)) {
          int num = input.toInt();
          if (num >= 120 && num <= 3600) {
            act->action = Dlg::None;
            myConfig.tgTimeoutSec = num;
            needUpdate = true;
            sendReconnectMessage("✅ Таймаут связи Telegram: " + String(num) + " с", userID);
            command = "/configure";
          } else {
            sendReconnectMessage(F("❌ Ожидалось значение от 120 до 3600 с!"), userID);
            return;
          }
        } else {
          LOG_D("Некорректный ввод пользователя");
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
        LOG_W("OTA: обновление прошивки от %s (%s)", username.c_str(), fileName.c_str());
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

          actionSet(userID, Dlg::Reset);
        }
        // 🔧 Ручная калибровка конкретного датчика
        else if (command.startsWith("/HumidityMCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          if (!validChannelIndex(ind, userID)) return;
          sendReconnectMessage("🔧 Введите минимальное и максимальное значение датчика № " + String(ind + 1) + " в формате: целое,целое.\nТекущее: [" + String(hs.getLow(ind)) + "; " + String(hs.getHigh(ind)) + "]", userID);
          actionSet(userID, Dlg::CalibManual + ind);
        }
        // 🔧 Автоматическая калибровка конкретного датчика
        else if (command.startsWith("/HumidityCalibrate")) {
          String prob = getValue(command, '_', 1);
          int ind = prob.toInt();
          if (!validChannelIndex(ind, userID)) return;
          sendReconnectMessage("💧 Подготовьте ёмкость с водой и впитывающую салфетку в зоне доступа датчика.\n🚀 Запустить калибровку датчика № " + String((ind + 1)) + "?", userID);

          fb::Keyboard kb;
          kb.addButton("🚀 СТАРТ").addButton("❌ ОТМЕНА");
          fb::Message m;
          m.text = "<Калибровка>";
          m.chatID = userID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, Dlg::CalibStart + ind);
        }
        // 🔧 Меню калибровки
        else if (command == "/Calibrate") {
          fb::InlineKeyboard menu;
          for (int i = 0; i < NUM_CHANNELS; i++) {
            menu.addButton("🌱 Датчик №" + String(i + 1), "/HumidityCalibrate_" + String(i), fb::KeyStyle::Primary).newRow();
          }
          menu.addButton("🔙 Назад", "/Configure", fb::KeyStyle::Default);
          showMenu(u, userID, "🔧 <b>Калибровка</b>", menu);
        }
        // 🔧 Ручная калибровка (меню)
        else if (command == "/CalibrateManual") {
          hs.setAll();
          fb::InlineKeyboard menu;
          for (int i = 0; i < NUM_CHANNELS; i++) {
            String btnText = "🌱 Датчик №" + String(i + 1) + " [" + String(hs.getLow(i)) + ";" + String(hs.getHigh(i)) + "] " + String(hs.Percent(i)) + "%";
            menu.addButton(btnText, "/HumidityMCalibrate_" + String(i), fb::KeyStyle::Primary).newRow();
          }
          menu.addButton("🔙 Назад", "/Configure", fb::KeyStyle::Default);
          showMenu(u, userID, "🔧 <b>Ручная Калибровка</b>", menu);
        }
        // 🗑️ Удаление папки года
        else if (command == "/DelFolder") {
          sendReconnectMessage(F("📅 Введите год удаления (формат YYYY):"), userID);
          actionSet(userID, Dlg::DelFolder);
        }
        // 🔧 Дельта калибровки
        else if (command == "/DeltaCalibration") {
          sendReconnectMessage(F("🔧 Введите значение (от 0 до 2048):"), userID);
          actionSet(userID, Dlg::DeltaCalib);
        }
        // 💧 Дельта влажности
        else if (command == "/DeltaHumidity") {
          sendReconnectMessage(F("💧 Введите значение (от 0 до 100) %:"), userID);
          actionSet(userID, Dlg::DeltaHum);
        }
        // 💪 Насос повышения давления
        else if (command == "/BoostPump") {
          sendReconnectMessage(F("💪 При скольких открытых клапанах включать насос повышения давления?\n"
                                 "1–8 — порог (включать при ≥ N открытых), 9 — никогда:"), userID);
          actionSet(userID, Dlg::BoostPump);
        }
        // 🧽 Порог контроля засора фильтра
        else if (command == "/ClogThreshold") {
          sendReconnectMessage(F("🧽 Порог = % от скорости ЧИСТОГО фильтра, НИЖЕ которого подаётся тревога.\n"
                                 "Чем меньше число — тем сильнее должен засориться фильтр.\n"
                                 "• 50 % — поток упал до половины (вдвое медленнее)\n"
                                 "• 30 % — поток упал до 30 % от нормы (сильный засор)\n"
                                 "Введите значение от 10 до 90 %:"), userID);
          actionSet(userID, Dlg::ClogThreshold);
        }
        // 🧽 Фильтр прочищен — сброс эталона и тревоги
        else if (command == "/FilterClean") {
          flowMonitorRecalibrate();
          sendReconnectMessage(F("🧽 Принято! Эталон потока сброшен — система заново измерит «чистую» скорость в ближайших поливах."), userID);
        }
        // 🔌 Таймаут потери связи с Telegram
        else if (command == "/TgTimeout") {
          sendReconnectMessage(F("🔌 Через сколько секунд без связи с Telegram считать её потерянной?\n"
                                 "Введите значение (от 120 до 3600 с):"), userID);
          actionSet(userID, Dlg::TgTimeout);
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

          actionSet(userID, Dlg::WorkRain);
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

          actionSet(userID, Dlg::WorkNight);
        }
        // ⚙️ Главное меню настроек. Ловим оба написания: "/Configure" приходит
        // с inline-кнопок, "/configure" — редирект после текстовых диалогов.
        else if (command == "/Configure" || command == "/configure") {
          String boostText = myConfig.boostPumpValves >= 9
                               ? String("выкл")
                               : String("при ≥ ") + String(myConfig.boostPumpValves) + " кл.";
          String menuText = "🔧 <b>Настройка</b>\n"
                          "🌙 Работа ночью " + String(myConfig.runOnNight ? "[✅]" : "[❌]") + "\n"
                          "🌧️ Работа под дождём " + String(myConfig.runOnRain ? "[✅]" : "[❌]") + "\n"
                          "💧 Дельта влажности % (" + String(myConfig.deltaHum) + ")\n"
                          "🔧 Дельта калибровки (" + String(myConfig.deltaCalibration) + ")\n"
                          "💪 Насос давления (" + boostText + ")\n"
                          "🧽 Контроль фильтра (тревога ниже " + String(myConfig.clogThresholdPercent) + "% нормы)\n"
                          "🔌 Таймаут связи Telegram (" + String(myConfig.tgTimeoutSec) + " с)";

          fb::InlineKeyboard menu;
          menu.addButton("🌙 Работа ночью", "/WorkAtNight", fb::KeyStyle::Primary).newRow()
              .addButton("🌧️ Работа под дождём", "/WorkAtRain", fb::KeyStyle::Primary).newRow()
              .addButton("💧 Дельта влажности", "/DeltaHumidity", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Дельта калибровки", "/DeltaCalibration", fb::KeyStyle::Primary).newRow()
              .addButton("💪 Насос давления", "/BoostPump", fb::KeyStyle::Primary).newRow()
              .addButton("🧽 Порог фильтра", "/ClogThreshold", fb::KeyStyle::Primary).newRow()
              .addButton("🧽 Фильтр прочищен", "/FilterClean", fb::KeyStyle::Success).newRow()
              .addButton("🔌 Таймаут Telegram", "/TgTimeout", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Калибровка", "/Calibrate", fb::KeyStyle::Primary).newRow()
              .addButton("🔧 Ручная калибровка", "/CalibrateManual", fb::KeyStyle::Primary).newRow()
              .addButton("🔄 Сброс настроек", "/DropSettings", fb::KeyStyle::Danger).newRow()
              .addButton("🗑️ Удаление файлов", "/DelFolder", fb::KeyStyle::Danger).newRow()
              .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);
          showMenu(u, userID, menuText, menu);
        }
        // 🔄 Перезагрузка системы
        else if (command == "/Restart") {
          for (uint8_t i = 0; i < userCount; i++) {
            User* user = &users[i];
            sendReconnectMessage("🔄 Система будет перезагружена!", user->userID);
          }

          fb::Keyboard kb;
          kb.addButton("✅ ДА").addButton("❌ НЕТ");
          fb::Message m;
          m.text = "<Перезагрузка>";
          m.chatID = chatID;
          m.setKeyboard(&kb);
          bot.sendMessage(m);

          actionSet(userID, Dlg::Restart);
        }
        // 👥 Меню управления пользователями
        else if (command == "/Users") {
          fb::InlineKeyboard menu;
          menu.addButton("📋 Список", "/UsersList", fb::KeyStyle::Primary).newRow()
              .addButton("⬆️ Повышение", "/UsersUpEdit", fb::KeyStyle::Success).newRow()
              .addButton("⬇️ Понижение", "/UsersDownEdit", fb::KeyStyle::Danger).newRow()
              .addButton("🗑️ Удаление", "/UsersDelete", fb::KeyStyle::Danger).newRow()
              .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);
          showMenu(u, userID, "👥 <b>Пользователи</b>", menu);
        }
        // 📋 Список пользователей
        else if (command == "/UsersList") {
          for (uint8_t i = 0; i < userCount; i++) {
            User* user = &users[i];
            sendReconnectMessage("👤 Пользователь ID: " + user->userID + "\n🎭 Роль: " + user->role, chatID);
          }
        }
        // ⬇️ Меню понижения прав
        else if (command == "/UsersDownEdit") {
          fb::InlineKeyboard menu;
          bool hasUsers = false;
          for (uint8_t i = 0; i < userCount; i++) {
            User* user = &users[i];
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
            LOG_D("Меню понижения пользователей");
          }
        }
        // ⬆️ Меню повышения прав
        else if (command == "/UsersUpEdit") {
          fb::InlineKeyboard menu;
          bool hasUsers = false;
          for (uint8_t i = 0; i < userCount; i++) {
            User* user = &users[i];
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
            LOG_D("Меню повышения пользователей");
          }
        }
        // 🗑️ Меню удаления пользователей
        else if (command == "/UsersDelete") {
          fb::InlineKeyboard menu;
          bool hasUsers = false;
          for (uint8_t i = 0; i < userCount; i++) {
            User* user = &users[i];
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
            LOG_D("Меню удаления пользователей");
          }
        }
        // ➕ Добавить пользователя
        else if (command.startsWith("/AddUser")) {
          String userId = getValue(command, '_', 1);
          if (findUser(userId) != nullptr) {
            sendReconnectMessage("❌ Данный пользователь уже есть в системе!", chatID);
          } else if (addUser(userId, 2)) {
            sendReconnectMessage("✅ Вас добавил " + username + " в систему как пользователя!", userId);
            sendReconnectMessage("✅ Регистрация пользователя успешно завершена!", chatID);
            saveUsers();
          } else {
            sendReconnectMessage("❌ Достигнут лимит пользователей (" + String(MAX_USERS) + ")!", chatID);
          }
        }
        // ⬆️ Повысить пользователя до админа
        else if (command.startsWith("/GradeUser")) {
          String userId = getValue(command, '_', 1);
          User* user = findUser(userId);
          if (user != nullptr) {
            if (user->role < 2) {
              sendReconnectMessage("ℹ️ Пользователь с ID " + userId + " уже администратор", chatID);
            } else {
              user->role = 1;
              LOG_W("Привилегии: %s повышен до администратора (кем: %s)", user->userID.c_str(), username.c_str());
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
          User* user = findUser(userId);
          if (user != nullptr) {
            if (user->role == 2) {
              sendReconnectMessage("ℹ️ Пользователь с ID " + userId + " уже пользователь", chatID);
            } else {
              user->role = 2;
              LOG_I("Привилегии: %s понижен до пользователя (кем: %s)", user->userID.c_str(), username.c_str());
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
          User* user = findUser(userId);
          if (user != nullptr) {
            if (user->role == 0) {
              sendReconnectMessage("⛔ Нельзя удалять главного администратора", chatID);
            } else {
              sendReconnectMessage("🗑️ Вас удалил " + username + " из системы!", user->userID);
              sendReconnectMessage("✅ Удаление пользователя " + user->userID + " успешно завершено!", chatID);
              removeUser(userId);
              saveUsers();  // 🔧 фикс: раньше удаление не сохранялось в EEPROM
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
        if (!validChannelIndex(ind, userID)) return;
        sendReconnectMessage(("🎯 Введите % порога срабатывания клапана № " + String((ind + 1)) + ":"), userID);
        actionSet(userID, Dlg::Border + ind);
      }
      // 📝 Переименование датчика
      else if (command.startsWith("/NamingsSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        if (!validChannelIndex(ind, userID)) return;
        sendReconnectMessage(("📝 Введите название датчика № " + String((ind + 1)) + ":"), userID);
        actionSet(userID, Dlg::Rename + ind);
      }
      // 🚰 Установка режима работы клапана
      else if (command.startsWith("/OperationModeSet")) {
        String prob = getValue(command, '_', 1);
        int ind = prob.toInt();
        if (!validChannelIndex(ind, userID)) return;
        sendReconnectMessage(F("🚰 Выберите режим работы!"), userID);

        fb::Keyboard kb;
        kb.addButton("✅ ВКЛ.").addButton("⛔ ВЫКЛ.").newRow()
            .addButton("🤖 АВТО").addButton("🏠 А.П.");
        fb::Message m;
        m.text = "<Режим>";
        m.chatID = userID;
        m.setKeyboard(&kb);
        bot.sendMessage(m);


        actionSet(userID, Dlg::ModeSet + ind);
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

        actionSet(userID, Dlg::ModeSetAll);
      }
      // ⬆️ Запрос на повышение прав
      else if (command == "/GradeMeUp") {
        for (uint8_t i = 0; i < userCount; i++) {
          User* user = &users[i];
          if (user->role < 2) {
            sendReconnectMessage("👤 Пользователь: " + username + " ID: " + userID + " просит поднять его в правах.\n/GradeUser_" + userID, user->userID);
          }
        }
        sendReconnectMessage("⏳ Ваша регистрация принята, ожидайте ответа от администратора", chatID);
      }
      // 🏠 Главное меню ("/Start" — с inline-кнопок, "/start" — команда Telegram)
      else if (command == "/Start" || command == "/start") {
        fb::InlineKeyboard menu;
        buildMainMenu(menu, check_user->role < 2);
        showMenu(u, userID, "🚀 <b>Запуск</b>", menu);
      }
      // 📝 Меню переименования датчиков
      else if (command == "/Namings") {
        fb::InlineKeyboard menu;
        for (int i = 0; i < NUM_CHANNELS; i++) {
          String btnText = "🌱 Датчик №" + String(i + 1) + " (" + String(myConfig.chanel[i].title) + ")";
          menu.addButton(btnText, "/NamingsSet_" + String(i), fb::KeyStyle::Primary).newRow();
        }
        menu.addButton("🔙 Назад", "/Control", fb::KeyStyle::Default);
        showMenu(u, userID, "📝 <b>Именование</b>", menu);
      }
      // 🎯 Меню установки порогов влажности
      else if (command == "/Borders") {
        fb::InlineKeyboard menu;
        for (int i = 0; i < NUM_CHANNELS; i++) {
          String btnText = "🚰 Клапан №" + String(i + 1) + " (" + String(myConfig.chanel[i].title) + ") <" + String(myConfig.chanel[i].border) + " %>";
          menu.addButton(btnText, "/BordersSet_" + String(i), fb::KeyStyle::Primary).newRow();
        }
        menu.addButton("🔙 Назад", "/Control", fb::KeyStyle::Default);
        showMenu(u, userID, "🎯 <b>Пороги срабатывания</b>", menu);
      }
      // 🚰 Меню режимов работы клапанов
      else if (command == "/OperationMode") {
        fb::InlineKeyboard menu;
        for (int i = 0; i < NUM_CHANNELS; i++) {
          String modeSymbol;
          if (myConfig.chanel[i].mode == Mode::Auto) modeSymbol = "➖";
          else if (myConfig.chanel[i].mode == Mode::AlwaysOn) modeSymbol = "✅";
          else if (myConfig.chanel[i].mode == Mode::AlwaysOff) modeSymbol = "⛔";
          else modeSymbol = "🏠";

          String btnText = "🚰 Клапан №" + String(i + 1) + " (" + String(myConfig.chanel[i].title) + ") [" + modeSymbol + "]";
          menu.addButton(btnText, "/OperationModeSet_" + String(i), fb::KeyStyle::Primary).newRow();
        }
        menu.addButton("🚰 Установить для всех", "/AllOperationModeSet", fb::KeyStyle::Success).newRow()
            .addButton("🔙 Назад", "/Control", fb::KeyStyle::Default);
        showMenu(u, userID, "🚰 <b>Режим работы</b>", menu);
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
        for (int i = 0; i < NUM_CHANNELS; i++) {
          status = status + String("\n");
          status = status + String("\n🌱 Канал № ") + String((i + 1)) + String(" (") + String(myConfig.chanel[i].title) + String(")");
          if (check_user->role == 0) {
            status = status + String("\n📟 Текущее значение АЦП: ") + String(hs.getCurrent(i));
          }
          status = status + String("\n💧 Текущая влажность: ") + String(hs.Percent(i)) + String(" %");
          status = status + String("\n🎯 Граничное значение: ") + String(myConfig.chanel[i].border) + String(" %");
          status = status + String("\n🚰 Клапан: ") + String((oldMode[i] == VState::ForcedClose || oldMode[i] == VState::CloseByHum) ? "⛔ закрыт" : oldMode[i] == VState::Hysteresis ? "➖ без контроля" : "✅ открыт");
          status = status + String("\n⚙️ Режим: ") + String(myConfig.chanel[i].mode == Mode::Auto ? "🤖 автоматический" : myConfig.chanel[i].mode == Mode::AlwaysOn ? "✅ постоянно открыт"
                                                                                      : myConfig.chanel[i].mode == Mode::AlwaysOff ? "⛔ постоянно закрыт"
                                                                                                                     : "🏠 автоматический (парник)");
        }
        // 💧 Добавляем информацию о расходе воды
        status = status + String("\n");
        status = status + String("\n💧 <b>Расход воды</b>");
        status = status + String("\n📟 За текущую сессию: ") + String(flowGetSessionLiters(), 3) + String(" л");
        status = status + String("\n📊 Общий расход: ") + String(flowGetTotalLiters(), 3) + String(" л");

        // 🧽 Контроль засора фильтра
        status = status + String("\n");
        status = status + String("\n🧽 <b>Фильтр</b>");
        uint8_t openNow = countValveOpen();
        status = status + String("\n📟 Скорость потока: ") + String(fmLastRate(), 2) + String(" л/мин");
        float fmBase = fmBaselineFor(openNow);
        if (openNow >= 1 && fmBase > 0.0) {
          status = status + String("\n📊 Эталон (") + String(openNow) + String(" кл.): ") + String(fmBase, 2) + String(" л/мин");
        }
        status = status + String("\n🎯 Тревога при потоке ниже ") + String(myConfig.clogThresholdPercent) + String(" % от нормы");
        status = status + String("\n") + String(fmIsClogged() ? "⚠️ Похоже, фильтр засорён — прочистите!" : "✅ Фильтр в норме");

        // 💪 Насос повышения давления: фактическое состояние + причина + настройка
        status = status + String("\n");
        status = status + String("\n💪 <b>Насос давления</b>");
        String pumpNow;
        if (!pumpIsOn()) {
          pumpNow = F("⛔ выключен");
        } else if (valveIsDraining()) {
          pumpNow = F("⚡ работает — пролив дренажа");
        } else if (pumpStart != 0) {
          pumpNow = F("⚡ работает — пусковой режим (30 с)");
        } else {
          pumpNow = F("⚡ работает — по порогу открытых клапанов");
        }
        status = status + String("\n📟 Сейчас: ") + pumpNow;
        status = status + String("\n🎯 Настройка: ") + (myConfig.boostPumpValves >= 9
                              ? String("только пусковые 30 с")
                              : String("держать при ≥ ") + String(myConfig.boostPumpValves) + " откр. кл.");

        LOG_D("Свободная память: %u байт", ESP.getFreeHeap());
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

        actionSet(userID, Dlg::SearchStart);
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

          actionSet(userID, Dlg::ClearFlow);
        
      }
      // ⚙️ Меню управления ("/Control" — с inline-кнопок, "/control" —
      // редирект после текстовых диалогов поиска датчика)
      else if (command == "/Control" || command == "/control") {
        fb::InlineKeyboard menu;
        menu.addButton("🚰 Режим работы", "/OperationMode", fb::KeyStyle::Primary).newRow()
            .addButton("📝 Названия", "/Namings", fb::KeyStyle::Primary).newRow()
            .addButton("🎯 Пороги срабатывания", "/Borders", fb::KeyStyle::Primary).newRow()
            .addButton("💧 Расход воды", "/WaterFlow", fb::KeyStyle::Primary).newRow()
            .addButton("🗑️ Пролив дренажа", "/Spillage", fb::KeyStyle::Danger).newRow()
            .addButton("🔍 Поиск датчика", "/Searching", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);
        showMenu(u, userID, "⚙️ <b>Управление</b>", menu);
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
        actionSet(userID, Dlg::GraphPeriod);
      }
      // 📊 График за конкретную дату
      else if (command == "/GraphicsTo") {
        sendReconnectMessage(F("📅 Введите дату в формате dd.mm.yyyy, за которую хотите отобразить график"), userID);
        actionSet(userID, Dlg::GraphDate);
      }
      // 📁 Файл за конкретную дату
      else if (command == "/FileTo") {
        sendReconnectMessage(F("📅 Введите дату в формате dd.mm.yyyy, за которую хотите получить файл"), userID);
        actionSet(userID, Dlg::SendFile);
      }
      // 📁 Файл за сегодня
      else if (command == "/FileToday") {
        Datime t = getDateTime();
        String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
        if (SD.exists(fn)) {
          File file = SD.open(fn, FILE_READ);
          if (!file) {
            LOG_W("Не удалось открыть файл");
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
            LOG_W("Не удалось открыть файл");
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

      // 📈 Меню отчётов. Отдельный if (не else-if): сюда попадают и редиректы,
      // выставленные выше по цепочке (например, «файл не найден» → "/reports").
      if (command == "/reports" || command == "/Reports") {
        fb::InlineKeyboard menu;
        menu.addButton("📊 Графики", "/Graphics", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл за вчера", "/FileYesterday", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл текущий", "/FileToday", fb::KeyStyle::Primary).newRow()
            .addButton("📁 Файл...", "/FileTo", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Start", fb::KeyStyle::Default);
        showMenu(u, userID, "📈 <b>Отчёты</b>", menu);
      }
      // 📊 Меню графиков (аналогично — ловит редиректы "/Graphics" из цепочки)
      if (command == "/Graphics") {
        fb::InlineKeyboard menu;
        menu.addButton("📊 График за вчера", "/GraphicsYesterday", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График за сегодня", "/GraphicsToday", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График за декаду", "/GraphicsDecade", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График за период", "/GraphicsPeriod", fb::KeyStyle::Primary).newRow()
            .addButton("📊 График...", "/GraphicsTo", fb::KeyStyle::Primary).newRow()
            .addButton("🔙 Назад", "/Reports", fb::KeyStyle::Default);
        showMenu(u, userID, "📊 <b>Графики</b>", menu);
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
      for (uint8_t i = 0; i < userCount; i++) {
        User* user = &users[i];
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
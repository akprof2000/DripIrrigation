// bot_transport.cpp 📡 Транспорт Telegram: отправка и рассылки
#include "bot_transport.h"
#include "telegram.h"   // dropCDCard/connectCDCard/sendStatus, botMonitor*
#include "objects.h"    // bot, dropped
#include "users.h"      // users[], userCount
#include "log.h"
#include <WiFi.h>

// ⏱️ Максимальное ожидание восстановления WiFi внутри одной отправки (мс).
//    Ограничено, потому что вызывается из loop(): полив не должен «замирать».
#define TX_WIFI_WAIT_MS 3000UL

// 📨 Отправка сообщения. Различаем два вида неудачи:
//   • ответ Telegram с ok=false (result.isError()) — ошибка API (400/403/429…):
//     сеть в порядке, повторять и трогать WiFi бессмысленно — логируем и выходим;
//   • пустой результат (FastBot2 возвращает Result() при обрыве/таймауте HTTP) —
//     транспорт. Тогда, и ТОЛЬКО если WiFi реально отвалился, делаем одну
//     короткую попытку переподключения и одну повторную отправку.
void sendReconnectMessage(String text, String id, bool kbRem) {
  for (int attempt = 0; attempt < 2; attempt++) {
    fb::Message msg;
    msg.setModeHTML();
    msg.text = text;        // 💬 Текст сообщения
    msg.chatID = id;        // 👤 ID чата получателя

    if (kbRem) {
      msg.removeKeyboard();
    }

    // 📤 Синхронная отправка (wait=true)
    fb::Result result = bot.sendMessage(msg, true);

    if (result.isError()) {
      // ❌ Ошибка уровня Telegram API — не сетевая, WiFi не трогаем
      LOG_W("Telegram API отказал (chat %s): %s %s", id.c_str(),
            result.getErrorCode().toString().c_str(), result.getError().toString().c_str());
      return;
    }
    if (!result.isEmpty()) {
      return;  // ✅ успешно отправлено
    }

    // 📡 Транспортная ошибка. Переподключаемся только при реальной потере WiFi
    if (WiFi.status() == WL_CONNECTED) {
      LOG_W("Telegram: нет ответа сервера (WiFi на месте) — chat %s", id.c_str());
      return;  // сервер/DNS/TLS — переподключение WiFi не поможет, повтор не делаем
    }
    if (attempt > 0) break;  // повторяем не более одного раза за вызов

    LOG_W("Ошибка отправки в Telegram — WiFi потерян, переподключение");
    dropped = true;  // 📡 Флаг потери соединения (снимет ReCheck при восстановлении)
    WiFi.reconnect();
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < TX_WIFI_WAIT_MS) {
      delay(100);
      yield();
    }
    if (WiFi.status() != WL_CONNECTED) {
      LOG_W("WiFi не восстановлен за %lu мс — отправка отложена", TX_WIFI_WAIT_MS);
      return;  // дальше сеть поднимет ReCheck() в фоне
    }
  }
}

// 💾 Уведомление об отключении SD-карты
void dropCDCard() {
  LOG_W("Нет SD-карты — рассылка уведомления пользователям");
  for (uint8_t i = 0; i < userCount; i++) {
    sendReconnectMessage(F("💾 Отсутствует SD карта — система не сможет работать!"), users[i].userID);
  }
}

// 💾 Уведомление о подключении SD-карты
void connectCDCard() {
  LOG_I("SD-карта снова доступна — рассылка пользователям");
  for (uint8_t i = 0; i < userCount; i++) {
    sendReconnectMessage(F("💾 SD карта снова активна!"), users[i].userID);
  }
}

// 📨 Рассылка статусного сообщения всем подписанным пользователям
void sendStatus(String text) {
  if (dropped) return;  // 📡 Не отправляем при отсутствии WiFi
  for (uint8_t i = 0; i < userCount; i++) {
    User* user = &users[i];
    if (user->messages) {
      sendReconnectMessage(text, user->userID);
    }
  }
}

// ============================================================
// 🔌 Контроль связи именно с серверами Telegram (не только WiFi)
// ============================================================
// FastBot2 не отдаёт событие «разрыв». Сигнал «связь жива» = любой полученный
// ответ от api.telegram.org (колбэк onRaw срабатывает на каждый ответ, включая
// пустой long-poll) или принятый апдейт (tick()==true). Разрыв определяем по
// отсутствию контакта дольше myConfig.tgTimeoutSec.
static unsigned long lastBotOkMs = 0;     // время последнего контакта с Telegram
static unsigned long botDownSinceMs = 0;  // когда связь пропала (для длительности)
static bool          botJustRestored = false;

// ✅ Зафиксировать контакт с Telegram (связь жива)
static void markBotContact() {
  lastBotOkMs = millis();
  if (botHasError) {            // были в офлайне → связь восстановилась
    botHasError = false;
    botJustRestored = true;
  }
}

// 📥 Колбэк сырого ответа сервера — фактом вызова подтверждает связь
static void onBotRaw(Text /*response*/) {
  markBotContact();
}

// 🔌 Подключить детект связи (вызывать в botInit, до опроса)
void botMonitorAttach() {
  bot.onRaw(onBotRaw);
  lastBotOkMs = millis();  // стартовая точка отсчёта
}

// ⏱️ Тик контроля связи — вызывать в ReCheck после bot.tick()
void botMonitorTick(bool gotUpdate) {
  unsigned long now = millis();
  if (gotUpdate) markBotContact();          // пришёл апдейт = связь жива
  if (lastBotOkMs == 0) lastBotOkMs = now;  // 🛡️ страховка инициализации

  // 🆘 Нет контакта дольше настраиваемого таймаута → Telegram недоступен
  unsigned long timeoutMs = (unsigned long)myConfig.tgTimeoutSec * 1000UL;
  if (!botHasError && (now - lastBotOkMs > timeoutMs)) {
    botHasError = true;
    botDownSinceMs = lastBotOkMs;
    LOG_W("Связь с Telegram потеряна");
  }

  // ✅ Единое уведомление о восстановлении (покрывает и WiFi, и Telegram-уровень).
  //    Шлём только когда WiFi уже поднят — иначе sendStatus подавится по dropped
  //    и сообщение потеряется, поэтому при dropped флаг НЕ сбрасываем.
  if (botJustRestored && !dropped) {
    botJustRestored = false;
    unsigned long downSec = (now - botDownSinceMs) / 1000;
    LOG_I("Связь восстановлена");
    sendStatus("🌐 Связь восстановлена (была недоступна "
               + String(downSec / 60) + " мин " + String(downSec % 60) + " с)");
  }
}

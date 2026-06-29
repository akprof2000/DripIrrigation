// irrigation.cpp 🌱 Реализация доменной логики полива (чистое ядро)
#include "irrigation.h"
#include "objects.h"  // Mode::, VState:: (только константы)

// 🧠 Чистое решение по одному каналу — точная копия прежней логики checkValve(),
// но без побочных эффектов: ничего не открывает и не шлёт, только вычисляет.
//
// Логика уведомления (notify) воспроизводит прежнее поведение:
//   • Open/Close/Forced — уведомляем при смене состояния (prev != new);
//   • Hysteresis — уведомляем только если пришли НЕ из {OpenByHum, CloseByHum,
//     Hysteresis} (т.е. из начального/ручного), иначе состояние не трогаем.
// Оболочка записывает newState в oldMode только когда notify == true — это
// в точности повторяет старую условную запись для гистерезиса.
ValveDecision decideValve(uint8_t mode, int humidity, int border,
                          int deltaHum, uint8_t prevState) {
  ValveDecision d;

  // 🤖 Автоматический режим или режим парника
  if (mode == Mode::Auto || mode == Mode::Greenhouse) {
    if (humidity < border - deltaHum) {           // ниже порога — открыть
      d.action = ValveAction::Open;
      d.newState = VState::OpenByHum;
      d.notify = (prevState != VState::OpenByHum);
    } else if (humidity > border + deltaHum) {     // выше порога — закрыть
      d.action = ValveAction::Close;
      d.newState = VState::CloseByHum;
      d.notify = (prevState != VState::CloseByHum);
    } else {                                        // зона гистерезиса — держать
      d.action = ValveAction::Hold;
      d.newState = VState::Hysteresis;
      d.notify = (prevState != VState::OpenByHum &&
                  prevState != VState::CloseByHum &&
                  prevState != VState::Hysteresis);
    }
  }
  // ✅ Ручной режим: постоянно открыт
  else if (mode == Mode::AlwaysOn) {
    d.action = ValveAction::Open;
    d.newState = VState::ForcedOpen;
    d.notify = (prevState != VState::ForcedOpen);
  }
  // ⛔ Ручной режим: постоянно закрыт
  else {
    d.action = ValveAction::Close;
    d.newState = VState::ForcedClose;
    d.notify = (prevState != VState::ForcedClose);
  }

  return d;
}

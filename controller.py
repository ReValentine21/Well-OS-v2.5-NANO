import time
import serial
import threading
from datetime import datetime
from pynput import mouse, keyboard

# --- НАСТРОЙКИ ПОРТА ---
SERIAL_PORT = "COM4"  # Замени на порт своей Arduino (например, COM4 или /dev/ttyUSB0)
BAUD_RATE = 115200

# Глобальное состояние системы (потокобезопасное)
state = {
    "x": 64,
    "y": 32,
    "click": 0,  # bit 0: left click, bit 1: sleep status
    "key": 0,  # ASCII код нажатой клавиши
    "last_send_time": 0
}

# Инициализация Serial соединения
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    time.sleep(2)  # Ожидаем перезагрузку Arduino
    print(f"[!] Успешно подключено к {SERIAL_PORT}")
except Exception as e:
    print(f"[X] Ошибка подключения к порту {SERIAL_PORT}: {e}")
    exit()


# --- ОБРАБОТЧИКИ СОБЫТИЙ МЫШИ ---
def on_move(x, y):
    # Карта координат: подгони под разрешение своего монитора, если нужно
    # pynput выдает координаты экрана ПК. Переводим их в сетку 128x64
    # Для простоты берем остаток от деления или масштабируем от стандартного fullHD
    mapped_x = int((x % 1920) / 1920 * 127)
    mapped_y = int((y % 1080) / 1080 * 63)

    state["x"] = max(0, min(127, mapped_x))
    state["y"] = max(0, min(63, mapped_y))


def on_click(x, y, button, pressed):
    if button == mouse.Button.left:
        if pressed:
            state["click"] |= 0x01  # Устанавливаем 0-й бит в 1
        else:
            state["click"] &= ~0x01  # Сбрасываем 0-й бит в 0


def on_scroll(x, y, dx, dy):
    # Колесико мыши: вверх передает '+', вниз передает '-'
    if dy > 0:
        state["key"] = 43  # ASCII код для '+'
    elif dy < 0:
        state["key"] = 45  # ASCII код для '-'


# --- ОБРАБОТЧИКИ СОБЫТИЙ КЛАВИАТУРЫ ---
def on_press(key):
    try:
        if hasattr(key, 'char') and key.char is not None:
            # Обычные символы (буквы, цифры)
            state["key"] = ord(key.char)
        else:
            # Спец-клавиши
            if key == keyboard.Key.enter:
                state["key"] = 13
            elif key == keyboard.Key.backspace:
                state["key"] = 8
            elif key == keyboard.Key.space:
                state["key"] = 32
    except Exception as e:
        print(f"Ошибка захвата клавиши: {e}")


# --- ПОТОК СТАБИЛЬНОЙ ОТПРАВКИ ДАННЫХ (БЕЗ ЛАГОВ) ---
def serial_sender_thread():
    print("[*] Поток отправки запущен. Консоль Wel-OS синхронизирована.")
    while True:
        now = datetime.now()

        # Сборка пакета
        pX = state["x"]
        pY = state["y"]
        pClick = state["click"]
        pKey = state["key"]
        pHr = now.hour
        pMn = now.minute
        pSc = now.second

        # Сброс байта клавиши после чтения, чтобы не спамить бесконечно одним символом
        state["key"] = 0

        # Вычисление контрольной суммы (как в Arduino)
        checksum = (pX + pY + pClick + pKey + pHr + pMn + pSc) & 0xFF

        # Формирование бинарного пакета [0xAA, 0x55, данные..., CRC]
        packet = bytes([0xAA, 0x55, pX, pY, pClick, pKey, pHr, pMn, pSc, checksum])

        try:
            ser.write(packet)
            ser.flush()
        except Exception as e:
            print(f"\n[X] Потеряно соединение со скетчем: {e}")
            break

        # Частота отправки пакетов: 40 Гц (каждые 25 миллисекунд)
        # Это предотвращает переполнение буфера обмена микроконтроллера!
        time.sleep(0.025)


# Запуск фонового потока трансляции
sender = threading.Thread(target=serial_sender_thread, daemon=True)
sender.start()

# Запуск слушателей периферии ПК в основном потоке
mouse_listener = mouse.Listener(on_move=on_move, on_click=on_click, on_scroll=on_scroll)
keyboard_listener = keyboard.Listener(on_press=on_press)

mouse_listener.start()
keyboard_listener.start()

# Удержание скрипта активным
try:
    mouse_listener.join()
    keyboard_listener.join()
except KeyboardInterrupt:
    print("\n Сервер Wel-OS успешно остановлен.")

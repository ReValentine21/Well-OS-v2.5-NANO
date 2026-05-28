#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Система курсора
int targetX = 64, targetY = 32;
float currentX = 64.0, currentY = 32.0;
bool currentClick = false, lastClick = false;
unsigned long lastClickTime = 0;
bool doubleClick = false;
int lastClickedIcon = -1;

// Данные ПК
byte sysHr = 0, sysMin = 0, sysSec = 0;
bool screenOff = false;

// Окна и приложения
byte activeApp = 0; // 0=Desktop, 1=Cmd, 2=Calc, 3=Mine, 4=Settings
byte appState[5] = {0, 0, 0, 0, 0}; 

// Иконки рабочего стола
int iconX[4] = {8, 38, 68, 98};
int iconY[4] = {12, 12, 12, 12};
const char* appIcons[4] = {">", "+", "*", "O"};

// Меню "Пуск"
bool showStartMenu = false;

// Настройки ОС
bool showSeconds = false;
bool darkMode = false;
bool autoSleep = true;
bool fastCursor = false;

// Буферы приложений
char cmdBuf[15]; byte cmdLen = 0;
long calcVal = 0, calcSaved = 0; char calcOp = ' ';
byte mineGrid[24]; bool mineOver = false; bool mineWon = false;

// Перезагрузка Arduino (софтверный Reset)
void(* resetFunc) (void) = 0;

bool hit(int mx, int my, int x, int y, int w, int h) {
  return (mx >= x && mx <= x + w && my >= y && my <= y + h);
}

// Генерация Сапёра
void initMines() {
  memset(mineGrid, 0, sizeof(mineGrid)); 
  mineOver = false; mineWon = false;
  // Ставим 4 мины
  int minesPlaced = 0;
  while(minesPlaced < 4) {
    int idx = random(0, 24);
    if(!(mineGrid[idx] & 0x10)) { mineGrid[idx] |= 0x10; minesPlaced++; }
  }
}

int countMines(int idx) {
  int r = idx/6, c = idx%6, cnt = 0;
  for(int dr=-1; dr<=1; dr++) for(int dc=-1; dc<=1; dc++) {
    int nr=r+dr, nc=c+dc;
    if(nr>=0 && nr<4 && nc>=0 && nc<6) if(mineGrid[nr*6+nc] & 0x10) cnt++;
  } return cnt;
}

// Парсер пакетов
byte rState = 0, pX, pY, pClick, pKey, pHr, pMn, pSc;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000); // Разгон шины для 60 FPS
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  randomSeed(analogRead(0));
  
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
  display.setCursor(26, 28); display.print(F("Wel-OS v2.5"));
  display.display(); delay(1000);
  initMines();
}

void loop() {
  // 1. ЧТЕНИЕ ПОРТА И ОГРАНИЧЕНИЕ КУРСОРА
  while (Serial.available() > 0) {
    byte b = Serial.read();
    if (rState==0) { if(b==0xAA) rState=1; } 
    else if (rState==1) { if(b==0x55) rState=2; else rState=0; }
    else if (rState==2) { pX=b; rState=3; }
    else if (rState==3) { pY=b; rState=4; }
    else if (rState==4) { pClick=b; rState=5; }
    else if (rState==5) { pKey=b; rState=6; }
    else if (rState==6) { pHr=b; rState=7; }
    else if (rState==7) { pMn=b; rState=8; }
    else if (rState==8) { pSc=b; rState=9; }
    else if (rState==9) {
      if (((pX + pY + pClick + pKey + pHr + pMn + pSc) & 0xFF) == b) {
        // ОГРАНИЧЕНИЕ ЭКРАНА: Курсор не выходит за рамки!
        targetX = constrain(pX, 0, 127); 
        targetY = constrain(pY, 0, 63);
        
        currentClick = (pClick & 0x01);
        bool pcSleep = (pClick >> 1) & 0x01;
        
        if (autoSleep && pcSleep) { if(!screenOff){ display.ssd1306_command(0xAE); screenOff=true; } } 
        else { if(screenOff){ display.ssd1306_command(0xAF); screenOff=false; } }
        
        sysHr = pHr; sysMin = pMn; sysSec = pSc;
        
        // Клавиатура (для терминала или калькулятора)
        if(pKey != 0) { 
          if(activeApp == 1) { // Cmd
            if (pKey == 13) { cmdBuf[cmdLen]='\0'; if(strcmp(cmdBuf,"clear")==0) cmdLen=0; }
            else if (pKey == 8 && cmdLen > 0) cmdBuf[--cmdLen]='\0';
            else if (cmdLen < 14 && pKey >= 32) cmdBuf[cmdLen++] = (char)pKey;
          }
          if(activeApp == 2) { // Клавиатура для калькулятора
             if(pKey >= '0' && pKey <= '9') calcVal = calcVal*10 + (pKey-'0');
             if(pKey == '+' || pKey == '-') { calcOp=pKey; calcSaved=calcVal; calcVal=0; }
             if(pKey == 13 || pKey == '=') {
               if(calcOp=='+') calcVal = calcSaved+calcVal; 
               if(calcOp=='-') calcVal = calcSaved-calcVal; calcOp=' '; 
             }
             if(pKey == 'c' || pKey == 'C' || pKey == 8) { calcVal=0; calcSaved=0; calcOp=' '; }
          }
        }
      }
      rState = 0;
    }
  }

  if (screenOff) return;

  // Плавность курсора (зависит от настроек)
  float easing = fastCursor ? 0.7 : 0.35;
  currentX += (targetX - currentX) * easing; 
  currentY += (targetY - currentY) * easing;
  int cx = (int)currentX; int cy = (int)currentY;

  // 2. ОБРАБОТКА КЛИКОВ
  bool justPressed = currentClick && !lastClick;
  doubleClick = false;
  
  if (justPressed) {
    int clickedIcon = -1;
    if(activeApp == 0 && !showStartMenu) {
      for(int i=0; i<4; i++) {
        if(hit(targetX, targetY, iconX[i], iconY[i], 20, 20)) { clickedIcon = i; break; }
      }
    }
    if (clickedIcon != -1 && clickedIcon == lastClickedIcon && (millis() - lastClickTime < 400)) {
      doubleClick = true; lastClickedIcon = -1;
    } else { lastClickedIcon = clickedIcon; }
    lastClickTime = millis();
  }

  if (doubleClick && activeApp == 0 && !showStartMenu) {
    for(int i=0; i<4; i++) {
      if(hit(targetX, targetY, iconX[i], iconY[i], 20, 20)) {
        activeApp = i + 1; appState[activeApp] = 1; doubleClick = false; break;
      }
    }
  }

  if (justPressed && !doubleClick) {
    // --- Обработка меню ПУСК ---
    if (showStartMenu) {
      if (hit(targetX, targetY, 2, 22, 48, 10)) { showStartMenu = false; display.ssd1306_command(0xAE); screenOff=true;} // Sleep
      else if (hit(targetX, targetY, 2, 32, 48, 10)) { resetFunc(); } // Reboot
      else if (hit(targetX, targetY, 2, 42, 48, 10)) { showStartMenu = false; } // Close
      else showStartMenu = false; // Клик мимо закрывает пуск
    }
    // --- ТАСКБАР ---
    else if (targetY >= 52) { 
      if (hit(targetX, targetY, 2, 53, 11, 10)) showStartMenu = true; // Кнопка [w]
      // Переключение приложений на таскбаре
      for(int i=1; i<=4; i++) {
        if (hit(targetX, targetY, 20 + (i-1)*14, 54, 12, 10) && appState[i]) activeApp = i;
      }
    }
    // --- ПОЛНОЭКРАННЫЕ ПРИЛОЖЕНИЯ ---
    else if (activeApp > 0) { 
      // Шапка (свернуть / закрыть)
      if (targetY <= 10) {
        if (hit(targetX, targetY, 116, 0, 12, 10)) { appState[activeApp]=0; activeApp=0; } // Close
        else if (hit(targetX, targetY, 104, 0, 12, 10)) { activeApp=0; } // Minimize
      }
      
      else if (activeApp == 2) { // Калькулятор
        int startX=4, startY=12;
        int bw=28, bh=9;
        // Цифры 1-9
        for(int i=0; i<9; i++) {
          if(hit(targetX, targetY, startX+(i%3)*(bw+2), startY+(i/3)*(bh+2), bw, bh)) calcVal = calcVal*10 + (i+1);
        }
        // Нижний ряд
        if(hit(targetX, targetY, startX, startY+33, bw, bh)) { calcVal=0; calcSaved=0; calcOp=' '; } // C
        if(hit(targetX, targetY, startX+30, startY+33, bw, bh)) calcVal = calcVal*10; // 0
        if(hit(targetX, targetY, startX+60, startY+33, bw, bh)) { // =
          if(calcOp=='+') calcVal = calcSaved+calcVal; 
          if(calcOp=='-') calcVal = calcSaved-calcVal; calcOp=' '; 
        }
        // Операторы
        if(hit(targetX, targetY, startX+90, startY, bw, bh+5)) { calcOp='+'; calcSaved=calcVal; calcVal=0; }
        if(hit(targetX, targetY, startX+90, startY+16, bw, bh+5)) { calcOp='-'; calcSaved=calcVal; calcVal=0; }
      }
      else if (activeApp == 3) { // Сапер
        if(hit(targetX, targetY, 100, 14, 24, 10)) initMines(); // Reset button
        if(!mineOver && !mineWon) {
        int revCount = 0;
          for(int i=0; i<24; i++) {
            if(hit(targetX, targetY, 32+(i%6)*11, 14+(i/6)*11, 10, 10)) {
              mineGrid[i] |= 0x20; if(mineGrid[i] & 0x10) mineOver = true;
            }
            if(mineGrid[i] & 0x20) revCount++;
          }
          if(revCount == 20) mineWon = true; // 24-4=20
        }
      }
      else if (activeApp == 4) { // Настройки (Settings)
        if(hit(targetX, targetY, 4, 14, 10, 10)) showSeconds = !showSeconds;
        if(hit(targetX, targetY, 4, 26, 10, 10)) { darkMode = !darkMode; display.invertDisplay(darkMode); }
        if(hit(targetX, targetY, 4, 38, 10, 10)) autoSleep = !autoSleep;
        if(hit(targetX, targetY, 68, 14, 10, 10)) fastCursor = !fastCursor;
      }
    }
  }
  lastClick = currentClick;

  // 3. ОТРИСОВКА (RENDER)
  display.clearDisplay();

  // --- РАБОЧИЙ СТОЛ ---
  if (activeApp == 0) {
    char* lbl[4] = {"Cmd", "Calc", "Mine", "Set"};
    for(int i=0; i<4; i++) {
      if(hit(targetX, targetY, iconX[i], iconY[i], 20, 20)) display.drawRect(iconX[i]-2, iconY[i]-2, 24, 24, SSD1306_WHITE);
      display.drawRect(iconX[i], iconY[i], 20, 20, SSD1306_WHITE);
      display.setCursor(iconX[i]+2, iconY[i]+22); display.print(lbl[i]);
      display.setCursor(iconX[i]+7, iconY[i]+6); display.print(appIcons[i]);
    }
  }
  // --- ПРИЛОЖЕНИЯ (ПОЛНОЭКРАННЫЕ) ---
  else {
    // Рисуем шапку программы (Title Bar)
    display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK); display.setCursor(2, 1);
    if(activeApp==1) display.print(F("TERMINAL")); if(activeApp==2) display.print(F("CALCULATOR"));
    if(activeApp==3) display.print(F("SAPER"));    if(activeApp==4) display.print(F("SETTINGS"));
    
    // Кнопки свернуть/закрыть
    display.drawRect(104, 0, 12, 10, SSD1306_BLACK); display.setCursor(107, 1); display.print(F("_"));
    display.fillRect(116, 0, 12, 10, SSD1306_BLACK); 
    display.setTextColor(SSD1306_WHITE); display.setCursor(119, 1); display.print(F("X"));
    
    if(activeApp == 1) { 
      display.setCursor(2, 14); display.print(F("C:\\>")); display.print(cmdBuf);
      if((millis()/500)%2==0) display.print(F("_"));
    }
    else if(activeApp == 2) { // Калькулятор UI
      // Экран калькулятора справа от операторов
      display.setCursor(94, 30); display.print(calcSaved); display.print(calcOp);
      display.setCursor(94, 40); display.print(calcVal);
      int startX=4, startY=12; int bw=28, bh=9;
      // Отрисовка сетки
      for(int i=0; i<9; i++) {
        display.drawRect(startX+(i%3)*(bw+2), startY+(i/3)*(bh+2), bw, bh, SSD1306_WHITE);
        display.setCursor(startX+11+(i%3)*(bw+2), startY+1+(i/3)*(bh+2)); display.print(i+1);
      }
      display.drawRect(startX, startY+33, bw, bh, SSD1306_WHITE); display.setCursor(startX+11, startY+34); display.print(F("C"));
      display.drawRect(startX+30, startY+33, bw, bh, SSD1306_WHITE); display.setCursor(startX+41, startY+34); display.print(F("0"));
      display.drawRect(startX+60, startY+33, bw, bh, SSD1306_WHITE); display.setCursor(startX+71, startY+34); display.print(F("="));
      display.drawRect(startX+90, startY, bw, bh+5, SSD1306_WHITE); display.setCursor(startX+101, startY+3); display.print(F("+"));
      display.drawRect(startX+90, startY+16, bw, bh+5, SSD1306_WHITE); display.setCursor(startX+101, startY+19); display.print(F("-"));
    }
    else if(activeApp == 3) { // Сапер UI
      display.drawRect(100, 14, 24, 10, SSD1306_WHITE); display.setCursor(103, 15); display.print(F("RST"));
      if(mineOver) { display.setCursor(100, 30); display.print(F("RIP")); }
      if(mineWon)  { display.setCursor(100, 30); display.print(F("WIN")); }
      
      for(int i=0; i<24; i++) { // 6x4 Сетка
        int mx = 32+(i%6)*11, my = 14+(i/6)*11;
        if(!(mineGrid[i] & 0x20)) {
          display.drawRect(mx, my, 10, 10, SSD1306_WHITE);
        } else {
          if(mineGrid[i] & 0x10) display.fillCircle(mx+4, my+4, 3, SSD1306_WHITE); // Мина
          else { 
            int n = countMines(i); 
            display.setCursor(mx+2, my+1); 
            if(n>0) display.print(n); else display.print(F(".")); 
          }
        }
      }
    }
    else if(activeApp == 4) { // Настройки UI
      display.drawRect(4, 14, 10, 10, SSD1306_WHITE); if(showSeconds) display.drawLine(4, 14, 14, 24, SSD1306_WHITE);
      display.setCursor(18, 15); display.print(F("Secs"));
      
      display.drawRect(4, 26, 10, 10, SSD1306_WHITE); if(darkMode) display.drawLine(4, 26, 14, 36, SSD1306_WHITE);
      display.setCursor(18, 27); display.print(F("Dark"));
      
      display.drawRect(4, 38, 10, 10, SSD1306_WHITE); if(autoSleep) display.drawLine(4, 38, 14, 48, SSD1306_WHITE);
      display.setCursor(18, 39); display.print(F("Sleep"));

      display.drawRect(68, 14, 10, 10, SSD1306_WHITE); if(fastCursor) display.drawLine(68, 14, 78, 24, SSD1306_WHITE);
      display.setCursor(82, 15); display.print(F("Fast"));
    }
  }

  // --- ТАСКБАР (НИЖНЯЯ ПАНЕЛЬ) ---
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  
  // Кнопка ПУСК [w]
  display.fillRect(2, 53, 11, 10, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK); display.setCursor(5, 54); display.print(F("w")); display.setTextColor(SSD1306_WHITE); 
  
  // Иконки активных приложений на таскбаре
  for(int i=1; i<=4; i++) {
    if(appState[i]) {
      int tx = 20 + (i-1)*14;
      if (activeApp == i) display.fillRect(tx, 54, 12, 10, SSD1306_WHITE);
      else display.drawRect(tx, 54, 12, 10, SSD1306_WHITE);
      
      display.setTextColor(activeApp == i ? SSD1306_BLACK : SSD1306_WHITE);
      display.setCursor(tx+3, 55); display.print(appIcons[i-1]);
      display.setTextColor(SSD1306_WHITE);
    }
  }

  // Часы
  if(showSeconds) display.setCursor(80, 55); else display.setCursor(96, 55);
  if(sysHr<10) display.print(F("0")); display.print(sysHr); display.print(F(":"));
  if(sysMin<10) display.print(F("0")); display.print(sysMin);
  if(showSeconds) { display.print(F(":")); if(sysSec<10) display.print(F("0")); display.print(sysSec); }

  // --- МЕНЮ ПУСК (ПОВЕРХ ВСЕГО) ---
  if(showStartMenu) {
    display.fillRect(0, 20, 52, 32, SSD1306_BLACK);
    display.drawRect(0, 20, 52, 32, SSD1306_WHITE);
    display.setCursor(4, 23); display.print(F("Sleep"));
    display.setCursor(4, 33); display.print(F("Reboot"));
    display.setCursor(4, 43); display.print(F("Close"));
    // Инверсия при наведении
    if(hit(targetX, targetY, 2, 22, 48, 10)) { display.fillRect(2, 22, 48, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); display.setCursor(4, 23); display.print(F("Sleep")); }
    if(hit(targetX, targetY, 2, 32, 48, 10)) { display.fillRect(2, 32, 48, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); display.setCursor(4, 33); display.print(F("Reboot")); }
    if(hit(targetX, targetY, 2, 42, 48, 10)) { display.fillRect(2, 42, 48, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); display.setCursor(4, 43); display.print(F("Close")); }
    display.setTextColor(SSD1306_WHITE);
  }

  // --- КУРСОР МЫШИ (Самый верхний слой) ---
  display.fillTriangle(cx, cy, cx+5, cy+2, cx+2, cy+5, SSD1306_WHITE);

  display.display();
}

#include "ScoreDisplay.h"

// --- 硬件引脚定义 (请根据你的实际接线修改) ---
// 红方显示器
const int RED_DATA  = 11; 
const int RED_CLOCK = 12;
const int RED_LATCH = 10;


// 创建显示对象
ScoreDisplay redDisplay(RED_DATA, RED_CLOCK, RED_LATCH);


void setup() {
    Serial.begin(115200);
    delay(1000); // 等待串口初始化

    // 初始化显示器
    redDisplay.begin();


    // 显示初始分数
    redDisplay.setScore(0);


    printMenu();
}

void loop() {
    // 检查是否有串口输入
    if (Serial.available() > 0) {
        char command = Serial.read(); // 读取一个字符
        
        switch (command) {
            case 'r': // 红方加分
                redDisplay.addScore();
                Serial.print("Red Scored! Current: ");
                Serial.println(redDisplay.getScore());
                break;
            case 'R': // 红方减分
                redDisplay.subScore();
                Serial.print("Red Subtracted. Current: ");
                Serial.println(redDisplay.getScore());
                break;
            case 'g': // 绿方加分
                redDisplay.addScore();
                Serial.print("Red Scored! Current: ");
                Serial.println(redDisplay.getScore());
                break;
            case 'G': // 绿方减分
               redDisplay.subScore();
                Serial.print("Red Subtracted. Current: ");
                Serial.println(redDisplay.getScore());
                break;
            case '0': // 全部归零
                redDisplay.reset();
 
                Serial.println("Scores Reset!");
                break;
            case 'h': // 显示帮助菜单
                printMenu();
                break;
            default:
                // 忽略换行符等字符
                break;
        }
    }
}

// 打印帮助菜单到串口
void printMenu() {
    Serial.println("\n--- Fencing Score Test System ---");
    Serial.println("r : Red +1");
    Serial.println("R : Red -1");
    Serial.println("g : Green +1");
    Serial.println("G : Green -1");
    Serial.println("0 : Reset All");
    Serial.println("h : Show this menu");
    Serial.println("---------------------------------");
}
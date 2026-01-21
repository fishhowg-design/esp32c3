#ifndef SCORE_MANAGER_H
#define SCORE_MANAGER_H

#include <Arduino.h>

// 定义比分变化回调函数类型
// 参数说明: 
//   - int: 红方最新分数
//   - int: 绿方最新分数
//   - bool: 是否是重置操作 (true=全部重置, false=仅单分更新)
typedef void (*ScoreChangeCallback)(int redScore, int greenScore, bool isReset);

// 比分管理类
class ScoreManager {
private:
  int _redScore;          // 红方分数
  int _greenScore;        // 绿方分数
  ScoreChangeCallback _callback; // 分数变化回调函数

public:
  // 构造函数
  ScoreManager();

  // 设置分数变化回调函数
  void setScoreChangeCallback(ScoreChangeCallback callback);

  // 红方加分
  void addRedScore();

  // 绿方加分
  void addGreenScore();

  // 双方同时加分 (平局场景)
  void addBothScores();

  // 获取红方分数
  int getRedScore() const;

  // 获取绿方分数
  int getGreenScore() const;

  // 重置分数
  // 参数: total - true=全部重置为0, false=仅标记为下一分开始(分数不重置)
  void reset(bool total);

  // 手动设置分数 (应急场景)
  void setScores(int red, int green, bool isReset = false);
};

#endif // SCORE_MANAGER_H
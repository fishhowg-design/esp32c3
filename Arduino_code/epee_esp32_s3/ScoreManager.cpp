#include "ScoreManager.h"

// 构造函数: 初始化分数为0，回调函数为空
ScoreManager::ScoreManager() 
  : _redScore(0), _greenScore(0), _callback(nullptr) {
}

// 设置分数变化回调函数
void ScoreManager::setScoreChangeCallback(ScoreChangeCallback callback) {
  _callback = callback;
}

// 红方加分
void ScoreManager::addRedScore() {
  _redScore++;
  // 触发回调 (isReset=false 表示是分数更新)
  if (_callback != nullptr) {
    _callback(_redScore, _greenScore, false);
  }
}

// 绿方加分
void ScoreManager::addGreenScore() {
  _greenScore++;
  if (_callback != nullptr) {
    _callback(_redScore, _greenScore, false);
  }
}

// 双方同时加分
void ScoreManager::addBothScores() {
  _redScore++;
  _greenScore++;
  if (_callback != nullptr) {
    _callback(_redScore, _greenScore, false);
  }
}

// 获取红方分数
int ScoreManager::getRedScore() const {
  return _redScore;
}

// 获取绿方分数
int ScoreManager::getGreenScore() const {
  return _greenScore;
}

// 重置分数
void ScoreManager::reset(bool total) {
  if (total) {
    _redScore = 0;
    _greenScore = 0;
  }
  // 触发回调 (isReset=true 表示是重置操作)
  if (_callback != nullptr) {
    _callback(_redScore, _greenScore, true);
  }
}

// 手动设置分数
void ScoreManager::setScores(int red, int green, bool isReset) {
  _redScore = red;
  _greenScore = green;
  if (_callback != nullptr) {
    _callback(_redScore, _greenScore, isReset);
  }
}
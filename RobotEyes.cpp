#include "RobotEyes.h"
#include <math.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

RobotEyes::RobotEyes()
    : _display(nullptr)
    , _screenW(128)
    , _screenH(64)
    , _maxFps(50)
    , _frameInterval(20)
    , _lastUpdate(0)
    , _currentFps(0)
    , _fpsCounter(0)
    , _fpsTimer(0)
    , _spaceBetween(8)
    , _posX(0)
    , _posY(0)
    , _position(POS_CENTER)
    , _emotion(EMOTION_NEUTRAL)
    , _targetEmotion(EMOTION_NEUTRAL)
    , _transitioning(false)
    , _transitionStart(0)
    , _transitionDuration(300)
    , _autoBlink(false)
    , _blinkTimer(0)
    , _blinkCooldown(0)
    , _breathing(false)
    , _breathTimer(0)
    , _breathPhase(0)
    , _sleepMode(false)
    , _idleMove(false)
    , _idleTimer(0)
    , _animPlaying(false)
    , _eyeColor(SSD1306_WHITE)
    , _pupilColor(SSD1306_BLACK)
    , _dirty(true)
    , _lastLeftEyelid(255)
    , _lastRightEyelid(255)
    , _lastLeftPupilX(0)
    , _lastLeftPupilY(0)
    , _lastRightPupilX(0)
    , _lastRightPupilY(0)
{
    // Initialize eyes
    _left.width = 30;
    _left.height = 30;
    _left.radius = 6;
    _left.pupilX = 0;
    _left.pupilY = 0;
    _left.pupilSize = 5;
    _left.eyelidOpen = 255;
    _left.targetEyelid = 255;
    _left.isOpen = true;
    
    _right = _left;
    _leftTarget = _left;
    _rightTarget = _right;
    
    setupNeutral();
}

RobotEyes::~RobotEyes() {}

// ============================================================================
// INITIALIZATION
// ============================================================================

void RobotEyes::begin(Adafruit_SSD1306* display, uint16_t screenW, uint16_t screenH) {
    begin(display, screenW, screenH, 50);
}

void RobotEyes::begin(Adafruit_SSD1306* display, uint16_t screenW, uint16_t screenH, uint8_t fps) {
    _display = display;
    _screenW = screenW;
    _screenH = screenH;
    _maxFps = fps;
    _frameInterval = 1000 / fps;
    _lastUpdate = millis();
    _dirty = true;
    calculatePositions();
}

// ============================================================================
// MAIN UPDATE
// ============================================================================

void RobotEyes::update() {
    uint32_t now = millis();
    
    // Frame rate control disabled for external loop
    // if (now - _lastUpdate < _frameInterval) {
    //     return;
    // }
    _lastUpdate = now;
    
    // FPS counter
    _fpsCounter++;
    if (now - _fpsTimer >= 1000) {
        _currentFps = _fpsCounter;
        _fpsCounter = 0;
        _fpsTimer = now;
    }
    
    // Update behaviors
    updateBehaviors();
    
    // Update animations
    updateAnimation();
    
    // Update emotion transition
    updateEmotionTransition();
    
    // Smooth eyelid following
    if (_left.eyelidOpen != _left.targetEyelid) {
        int diff = _left.targetEyelid - _left.eyelidOpen;
        _left.eyelidOpen += (diff > 0) ? max(1, diff / 3) : min(-1, diff / 3);
        if (abs(diff) < 3) _left.eyelidOpen = _left.targetEyelid;
        _dirty = true;
    }
    if (_right.eyelidOpen != _right.targetEyelid) {
        int diff = _right.targetEyelid - _right.eyelidOpen;
        _right.eyelidOpen += (diff > 0) ? max(1, diff / 3) : min(-1, diff / 3);
        if (abs(diff) < 3) _right.eyelidOpen = _right.targetEyelid;
        _dirty = true;
    }
    
    // Draw always
    drawEyes();
    _dirty = false;
}

// ============================================================================
// DRAW EYES
// ============================================================================

void RobotEyes::drawEyes() {
    if (!_display) return;
    
    // _display->clearDisplay(); // Disabled so custom UI isn't erased
    calculatePositions();
    
    drawEye(0);
    drawEye(1);
    
    // _display->display(); // Disabled so custom UI can be added before displaying
}

void RobotEyes::drawEye(uint8_t index) {
    EyeState* eye = (index == 0) ? &_left : &_right;
    uint16_t x = (index == 0) ? _left.pupilX : _right.pupilX;
    uint16_t y = (index == 0) ? _left.pupilY : _right.pupilY;
    
    // Effective height based on eyelid
    uint8_t effH = (eye->eyelidOpen * eye->height) >> 8;
    if (effH < 2) effH = 2;
    
    uint16_t hw = eye->width >> 1;
    uint16_t hh = effH >> 1;
    
    // Draw eye - rounded rect
    _display->fillRoundRect(x - hw, y - hh, eye->width, effH, eye->radius, _eyeColor);
    
    // Draw pupil if open enough
    if (eye->eyelidOpen > 80) {
        int16_t px = (eye->pupilX * (eye->width >> 2)) >> 4;
        int16_t py = (eye->pupilY * (effH >> 2)) >> 4;
        _display->fillCircle(x + px, y + py, eye->pupilSize, _pupilColor);
    }
    
    // Draw eyelid shadow if partially closed
    if (eye->eyelidOpen < 240 && eye->eyelidOpen > 10) {
        uint16_t covered = eye->height - effH;
        uint16_t halfCover = covered >> 1;
        
        if (halfCover > 0) {
            _display->fillRect(x - hw, y - hh, eye->width, halfCover, SSD1306_BLACK);
            _display->fillRect(x - hw, y + hh - halfCover, eye->width, halfCover + 1, SSD1306_BLACK);
        }
    }
}

// ============================================================================
// BEHAVIORS - FIXED
// ============================================================================

void RobotEyes::updateBehaviors() {
    uint32_t now = millis();
    
    // ========================================================================
    // AUTOBLINKER - MUCH SLOWER, NATURAL BLINKING
    // ========================================================================
    if (_autoBlink && !_animPlaying && !_sleepMode) {
        if (now >= _blinkTimer && now >= _blinkCooldown) {
            if (_left.eyelidOpen > 200 && _right.eyelidOpen > 200) {
                playBlink();
                // Next blink in 4-8 seconds (more natural)
                _blinkTimer = now + random(4000, 8000);
                _blinkCooldown = now + 800;
            }
        }
    }
    
    // ========================================================================
    // BREATHING - GENTLE EYE SIZE VARIATION
    // ========================================================================
    if (_breathing && !_sleepMode) {
        if (now - _breathTimer > 20) {
            _breathTimer = now;
            _breathPhase += 0.04f;
            if (_breathPhase > 6.28f) _breathPhase -= 6.28f;
            
            // Increased from 0.006f to 0.05f so the size actually changes
            float b = 1.0f + sinf(_breathPhase) * 0.05f; 
            _left.width = (uint8_t)(_leftTarget.width * b);
            _right.width = (uint8_t)(_rightTarget.width * b);
            _left.height = (uint8_t)(_leftTarget.height * b);
            _right.height = (uint8_t)(_rightTarget.height * b);
            _dirty = true;
        }
    }
    
    // ========================================================================
    // IDLE MOVEMENT - GENTLE GAZE SHIFTS
    // ========================================================================
    if (_idleMove && !_animPlaying && !_sleepMode) {
        if (now >= _idleTimer) {
            // Pick a new target for pupils to look around
            _leftTarget.pupilX = _emotionConfigs[_emotion].pupilX + random(-3, 4);
            _leftTarget.pupilY = _emotionConfigs[_emotion].pupilY + random(-2, 3);
            _rightTarget.pupilX = _leftTarget.pupilX;
            _rightTarget.pupilY = _leftTarget.pupilY;
            _idleTimer = now + random(1500, 4000); // Look around more often
            _dirty = true;
        }
        
        // Smoothly interpolate current pupil position towards target
        if (_left.pupilX != _leftTarget.pupilX) {
            _left.pupilX += (_leftTarget.pupilX > _left.pupilX) ? 1 : -1;
            _right.pupilX = _left.pupilX;
            _dirty = true;
        }
        if (_left.pupilY != _leftTarget.pupilY) {
            _left.pupilY += (_leftTarget.pupilY > _left.pupilY) ? 1 : -1;
            _right.pupilY = _left.pupilY;
            _dirty = true;
        }
    }
    
    // ========================================================================
    // SLEEP MODE
    // ========================================================================
    if (_sleepMode) {
        if (now - _lastUpdate > 200) {
            if (random(0, 50) == 0) {
                _left.pupilX = random(-2, 3);
                _left.pupilY = random(-2, 3);
                _right.pupilX = random(-2, 3);
                _right.pupilY = random(-2, 3);
                _dirty = true;
            }
        }
    }
}

// ============================================================================
// ANIMATION SYSTEM - FIXED: BLINK IS FAST, NO LONG CLOSED EYES
// ============================================================================

void RobotEyes::updateAnimation() {
    if (!_animPlaying) return;
    
    uint32_t now = millis();
    uint32_t elapsed = now - _anim.startTime;
    
    if (elapsed >= _anim.duration) {
        _animPlaying = false;
        // Always open eyes after animation
        openEyes();
        return;
    }
    
    _anim.progress = (float)elapsed / _anim.duration;
    
    // Simple blink animation: close, then immediately open
    if (_anim.phase == 0) {
        // Fast close
        if (_anim.progress < 0.3f) {
            float closeT = _anim.progress / 0.3f;
            uint8_t closeAmount = (uint8_t)(closeT * 255);
            _left.targetEyelid = 255 - closeAmount;
            _right.targetEyelid = _left.targetEyelid;
            _dirty = true;
        } 
        // Hold closed briefly
        else if (_anim.progress < 0.5f) {
            _left.targetEyelid = 0;
            _right.targetEyelid = 0;
            _dirty = true;
        }
        // Fast open
        else {
            float openT = (_anim.progress - 0.5f) / 0.5f;
            uint8_t openAmount = (uint8_t)(openT * 255);
            _left.targetEyelid = openAmount;
            _right.targetEyelid = _left.targetEyelid;
            _dirty = true;
        }
    }
}

void RobotEyes::updateEmotionTransition() {
    if (!_transitioning) return;
    
    uint32_t now = millis();
    uint32_t elapsed = now - _transitionStart;
    
    if (elapsed >= _transitionDuration) {
        _transitioning = false;
        applyEmotion(_targetEmotion);
        _emotion = _targetEmotion;
        // Ensure eyes are open after transition
        _left.targetEyelid = 255;
        _right.targetEyelid = 255;
        _dirty = true;
        return;
    }
    
    // Smooth transition - gradually change eye parameters
    float t = easeInOut((float)elapsed / _transitionDuration);
    
    // Interpolate eye shape
    uint8_t targetW = _emotionConfigs[_targetEmotion].eyeWidth;
    uint8_t targetH = _emotionConfigs[_targetEmotion].eyeHeight;
    uint8_t targetR = _emotionConfigs[_targetEmotion].borderRadius;
    
    _leftTarget.width = _leftTarget.width + (uint8_t)((targetW - _leftTarget.width) * t * 0.1f);
    _rightTarget.width = _leftTarget.width;
    _leftTarget.height = _leftTarget.height + (uint8_t)((targetH - _leftTarget.height) * t * 0.1f);
    _rightTarget.height = _leftTarget.height;
    _leftTarget.radius = _leftTarget.radius + (uint8_t)((targetR - _leftTarget.radius) * t * 0.1f);
    _rightTarget.radius = _leftTarget.radius;
    
    // Interpolate pupil position
    int8_t targetPX = _emotionConfigs[_targetEmotion].pupilX;
    int8_t targetPY = _emotionConfigs[_targetEmotion].pupilY;
    _leftTarget.pupilX += (int8_t)((targetPX - _leftTarget.pupilX) * t * 0.1f);
    _leftTarget.pupilY += (int8_t)((targetPY - _leftTarget.pupilY) * t * 0.1f);
    _rightTarget.pupilX = _leftTarget.pupilX;
    _rightTarget.pupilY = _leftTarget.pupilY;
    
    // Interpolate pupil size
    uint8_t targetPS = _emotionConfigs[_targetEmotion].pupilSize;
    _leftTarget.pupilSize += (uint8_t)((targetPS - _leftTarget.pupilSize) * t * 0.1f);
    _rightTarget.pupilSize = _leftTarget.pupilSize;
    
    // Keep eyes open during transition
    _left.targetEyelid = 255;
    _right.targetEyelid = 255;
    _dirty = true;
}

// ============================================================================
// EMOTION CONFIGURATIONS - WITH STORED CONFIG FOR TRANSITIONS
// ============================================================================

void RobotEyes::applyEmotion(EyeEmotion emotion) {
    // Store current emotion
    _emotion = emotion;
    
    // Apply emotion configuration directly
    switch (emotion) {
        case EMOTION_NEUTRAL: setupNeutral(); break;
        case EMOTION_HAPPY: setupHappy(); break;
        case EMOTION_ANGRY: setupAngry(); break;
        case EMOTION_SAD: setupSad(); break;
        case EMOTION_SLEEPY: setupSleepy(); break;
        case EMOTION_SURPRISED: setupSurprised(); break;
        case EMOTION_CURIOUS: setupCurious(); break;
        case EMOTION_SCARED: setupScared(); break;
        case EMOTION_LOVE: setupLove(); break;
        case EMOTION_LAUGHING: setupLaughing(); break;
        case EMOTION_THINKING: setupThinking(); break;
        case EMOTION_WINK_LEFT: setupWinkLeft(); break;
        case EMOTION_WINK_RIGHT: setupWinkRight(); break;
        default: setupNeutral(); break;
    }
    
    // Ensure eyes are open
    _left.targetEyelid = 255;
    _right.targetEyelid = 255;
    _dirty = true;
}

void RobotEyes::setupNeutral() {
    _leftTarget.width = 22; _rightTarget.width = 22;
    _leftTarget.height = 32; _rightTarget.height = 32;
    _leftTarget.radius = 11; _rightTarget.radius = 11;
    _leftTarget.pupilSize = 4; _rightTarget.pupilSize = 4;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 0;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 0;
    _leftTarget.targetEyelid = 255; _rightTarget.targetEyelid = 255;
    _posX = 0; _posY = 0;
    _spaceBetween = 12; // Increase space since eyes are narrower
    
    // Store config for transitions
    _emotionConfigs[EMOTION_NEUTRAL] = {22, 32, 11, 4, 0, 0, 255, 0, 0};
}

void RobotEyes::setupHappy() {
    _leftTarget.width = 28; _rightTarget.width = 28;
    _leftTarget.height = 24; _rightTarget.height = 24;
    _leftTarget.radius = 10; _rightTarget.radius = 10;
    _leftTarget.pupilSize = 6; _rightTarget.pupilSize = 6;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 3;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 3;
    _leftTarget.targetEyelid = 255; _rightTarget.targetEyelid = 255;
    _spaceBetween = 6;
    _emotionConfigs[EMOTION_HAPPY] = {28, 24, 10, 6, 0, 3, 255, 0, 0};
}

void RobotEyes::setupAngry() {
    _leftTarget.width = 26; _rightTarget.width = 26;
    _leftTarget.height = 28; _rightTarget.height = 28;
    _leftTarget.radius = 3; _rightTarget.radius = 3;
    _leftTarget.pupilSize = 4; _rightTarget.pupilSize = 4;
    _leftTarget.pupilX = -2; _leftTarget.pupilY = -3;
    _rightTarget.pupilX = 2; _rightTarget.pupilY = -3;
    _leftTarget.targetEyelid = 200; _rightTarget.targetEyelid = 200;
    _spaceBetween = 12;
    _emotionConfigs[EMOTION_ANGRY] = {26, 28, 3, 4, -2, -3, 200, 0, 0};
}

void RobotEyes::setupSad() {
    _leftTarget.width = 28; _rightTarget.width = 28;
    _leftTarget.height = 26; _rightTarget.height = 26;
    _leftTarget.radius = 8; _rightTarget.radius = 8;
    _leftTarget.pupilSize = 5; _rightTarget.pupilSize = 5;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 4;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 4;
    _leftTarget.targetEyelid = 180; _rightTarget.targetEyelid = 180;
    _spaceBetween = 10;
    _emotionConfigs[EMOTION_SAD] = {28, 26, 8, 5, 0, 4, 180, 0, 0};
}

void RobotEyes::setupSleepy() {
    _leftTarget.width = 26; _rightTarget.width = 26;
    _leftTarget.height = 16; _rightTarget.height = 16;
    _leftTarget.radius = 6; _rightTarget.radius = 6;
    _leftTarget.pupilSize = 3; _rightTarget.pupilSize = 3;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = -2;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = -2;
    _leftTarget.targetEyelid = 80; _rightTarget.targetEyelid = 80;
    _spaceBetween = 8;
    _emotionConfigs[EMOTION_SLEEPY] = {26, 16, 6, 3, 0, -2, 80, 0, 0};
}

void RobotEyes::setupSurprised() {
    _leftTarget.width = 36; _rightTarget.width = 36;
    _leftTarget.height = 36; _rightTarget.height = 36;
    _leftTarget.radius = 16; _rightTarget.radius = 16;
    _leftTarget.pupilSize = 3; _rightTarget.pupilSize = 3;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 0;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 0;
    _leftTarget.targetEyelid = 255; _rightTarget.targetEyelid = 255;
    _spaceBetween = 6;
    _emotionConfigs[EMOTION_SURPRISED] = {36, 36, 16, 3, 0, 0, 255, 0, 0};
}

void RobotEyes::setupCurious() {
    _leftTarget.width = 30; _rightTarget.width = 30;
    _leftTarget.height = 28; _rightTarget.height = 28;
    _leftTarget.radius = 6; _rightTarget.radius = 6;
    _leftTarget.pupilSize = 6; _rightTarget.pupilSize = 6;
    _leftTarget.pupilX = 4; _leftTarget.pupilY = 2;
    _rightTarget.pupilX = 4; _rightTarget.pupilY = 2;
    _leftTarget.targetEyelid = 255; _rightTarget.targetEyelid = 255;
    _spaceBetween = 6;
    _emotionConfigs[EMOTION_CURIOUS] = {30, 28, 6, 6, 4, 2, 255, 0, 0};
}

void RobotEyes::setupScared() {
    _leftTarget.width = 34; _rightTarget.width = 34;
    _leftTarget.height = 28; _rightTarget.height = 28;
    _leftTarget.radius = 6; _rightTarget.radius = 6;
    _leftTarget.pupilSize = 3; _rightTarget.pupilSize = 3;
    _leftTarget.pupilX = -3; _leftTarget.pupilY = -2;
    _rightTarget.pupilX = 3; _rightTarget.pupilY = -2;
    _leftTarget.targetEyelid = 220; _rightTarget.targetEyelid = 220;
    _spaceBetween = 4;
    _emotionConfigs[EMOTION_SCARED] = {34, 28, 6, 3, -3, -2, 220, 0, 0};
}

void RobotEyes::setupLove() {
    _leftTarget.width = 26; _rightTarget.width = 26;
    _leftTarget.height = 28; _rightTarget.height = 28;
    _leftTarget.radius = 14; _rightTarget.radius = 14;
    _leftTarget.pupilSize = 7; _rightTarget.pupilSize = 7;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 2;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 2;
    _leftTarget.targetEyelid = 255; _rightTarget.targetEyelid = 255;
    _spaceBetween = 4;
    _emotionConfigs[EMOTION_LOVE] = {26, 28, 14, 7, 0, 2, 255, 0, 0};
}

void RobotEyes::setupLaughing() {
    _leftTarget.width = 24; _rightTarget.width = 24;
    _leftTarget.height = 18; _rightTarget.height = 18;
    _leftTarget.radius = 12; _rightTarget.radius = 12;
    _leftTarget.pupilSize = 5; _rightTarget.pupilSize = 5;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 3;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 3;
    _leftTarget.targetEyelid = 255; _rightTarget.targetEyelid = 255;
    _spaceBetween = 4;
    _emotionConfigs[EMOTION_LAUGHING] = {24, 18, 12, 5, 0, 3, 255, 0, 0};
}

void RobotEyes::setupThinking() {
    _leftTarget.width = 26; _rightTarget.width = 26;
    _leftTarget.height = 26; _rightTarget.height = 26;
    _leftTarget.radius = 6; _rightTarget.radius = 6;
    _leftTarget.pupilSize = 4; _rightTarget.pupilSize = 4;
    _leftTarget.pupilX = 5; _leftTarget.pupilY = -2;
    _rightTarget.pupilX = -5; _rightTarget.pupilY = -2;
    _leftTarget.targetEyelid = 200; _rightTarget.targetEyelid = 200;
    _posX = -4; _posY = -4;
    _spaceBetween = 10;
    _emotionConfigs[EMOTION_THINKING] = {26, 26, 6, 4, 5, -2, 200, -4, -4};
}

void RobotEyes::setupWinkLeft() {
    _leftTarget.width = 30; _rightTarget.width = 30;
    _leftTarget.height = 30; _rightTarget.height = 30;
    _leftTarget.radius = 6; _rightTarget.radius = 6;
    _leftTarget.pupilSize = 5; _rightTarget.pupilSize = 5;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 0;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 0;
    _leftTarget.targetEyelid = 0;
    _rightTarget.targetEyelid = 255;
    _posX = 0; _posY = 0;
    _spaceBetween = 8;
    _emotionConfigs[EMOTION_WINK_LEFT] = {30, 30, 6, 5, 0, 0, 0, 0, 0};
}

void RobotEyes::setupWinkRight() {
    _leftTarget.width = 30; _rightTarget.width = 30;
    _leftTarget.height = 30; _rightTarget.height = 30;
    _leftTarget.radius = 6; _rightTarget.radius = 6;
    _leftTarget.pupilSize = 5; _rightTarget.pupilSize = 5;
    _leftTarget.pupilX = 0; _leftTarget.pupilY = 0;
    _rightTarget.pupilX = 0; _rightTarget.pupilY = 0;
    _leftTarget.targetEyelid = 255;
    _rightTarget.targetEyelid = 0;
    _posX = 0; _posY = 0;
    _spaceBetween = 8;
    _emotionConfigs[EMOTION_WINK_RIGHT] = {30, 30, 6, 5, 0, 0, 255, 0, 0};
}

// ============================================================================
// POSITION CALCULATION
// ============================================================================

void RobotEyes::calculatePositions() {
    uint16_t cx = _screenW >> 1;
    uint16_t cy = _screenH >> 1;
    
    int16_t halfSpace = (_left.width + _right.width + (_spaceBetween << 1)) >> 2;
    
    _left.pupilX = cx - halfSpace + _posX;
    _left.pupilY = cy + _posY;
    _right.pupilX = cx + halfSpace + _posX;
    _right.pupilY = cy + _posY;
}

// ============================================================================
// EASING FUNCTIONS
// ============================================================================

float RobotEyes::easeInOut(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

float RobotEyes::easeOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void RobotEyes::setEyeSize(uint8_t w, uint8_t h) {
    _leftTarget.width = constrain(w, 16, 44);
    _rightTarget.width = _leftTarget.width;
    _leftTarget.height = constrain(h, 12, 44);
    _rightTarget.height = _leftTarget.height;
    _dirty = true;
}

void RobotEyes::setBorderRadius(uint8_t r) {
    _leftTarget.radius = constrain(r, 2, 20);
    _rightTarget.radius = _leftTarget.radius;
    _dirty = true;
}

void RobotEyes::setSpaceBetween(int16_t space) {
    _spaceBetween = constrain(space, 2, 30);
    _dirty = true;
}

void RobotEyes::setPosition(EyePosition pos) {
    _position = pos;
    switch (pos) {
        case POS_N: _posX = 0; _posY = -8; break;
        case POS_NE: _posX = 8; _posY = -8; break;
        case POS_E: _posX = 8; _posY = 0; break;
        case POS_SE: _posX = 8; _posY = 8; break;
        case POS_S: _posX = 0; _posY = 8; break;
        case POS_SW: _posX = -8; _posY = 8; break;
        case POS_W: _posX = -8; _posY = 0; break;
        case POS_NW: _posX = -8; _posY = -8; break;
        default: _posX = 0; _posY = 0; break;
    }
    _dirty = true;
}

void RobotEyes::setPupilOffset(int8_t x, int8_t y) {
    _leftTarget.pupilX = constrain(x, -8, 8);
    _leftTarget.pupilY = constrain(y, -8, 8);
    _rightTarget.pupilX = _leftTarget.pupilX;
    _rightTarget.pupilY = _leftTarget.pupilY;
    _dirty = true;
}

void RobotEyes::setPupilPosition(int8_t x, int8_t y) {
    setPupilOffset(x, y);
}

void RobotEyes::setPupils(int8_t lx, int8_t ly, int8_t rx, int8_t ry) {
    _leftTarget.pupilX = constrain(lx, -8, 8);
    _leftTarget.pupilY = constrain(ly, -8, 8);
    _rightTarget.pupilX = constrain(rx, -8, 8);
    _rightTarget.pupilY = constrain(ry, -8, 8);
    _dirty = true;
}

void RobotEyes::setPupilSize(uint8_t size) {
    _leftTarget.pupilSize = constrain(size, 2, 10);
    _rightTarget.pupilSize = _leftTarget.pupilSize;
    _dirty = true;
}

void RobotEyes::setEyelidOpen(uint8_t amount) {
    _left.targetEyelid = constrain(amount, 0, 255);
    _right.targetEyelid = _left.targetEyelid;
    _dirty = true;
}

void RobotEyes::setEyelids(uint8_t left, uint8_t right) {
    _left.targetEyelid = constrain(left, 0, 255);
    _right.targetEyelid = constrain(right, 0, 255);
    _dirty = true;
}

void RobotEyes::openEyes() {
    _left.targetEyelid = 255;
    _right.targetEyelid = 255;
    _left.isOpen = true;
    _right.isOpen = true;
    _dirty = true;
}

void RobotEyes::closeEyes() {
    _left.targetEyelid = 0;
    _right.targetEyelid = 0;
    _left.isOpen = false;
    _right.isOpen = false;
    _dirty = true;
}

void RobotEyes::setEmotion(EyeEmotion emotion) {
    _emotion = emotion;
    _targetEmotion = emotion;
    _transitioning = false;
    applyEmotion(emotion);
}

void RobotEyes::setEmotionWithTransition(EyeEmotion emotion, uint16_t ms) {
    _targetEmotion = emotion;
    _transitioning = true;
    _transitionStart = millis();
    _transitionDuration = ms;
    _dirty = true;
}

EyeEmotion RobotEyes::getEmotion() const {
    return _emotion;
}

void RobotEyes::setAutoblinker(bool enable) {
    _autoBlink = enable;
    if (enable) {
        _blinkTimer = millis() + random(4000, 6000);
        _blinkCooldown = 0;
    }
}

void RobotEyes::setBreathing(bool enable) {
    _breathing = enable;
    if (enable) {
        _breathTimer = millis();
        _breathPhase = 0;
    }
}

void RobotEyes::setSleepMode(bool enable) {
    if (enable) {
        _sleepMode = true;
        closeEyes();
    } else {
        _sleepMode = false;
        openEyes();
    }
}

void RobotEyes::setIdleMovement(bool enable) {
    _idleMove = enable;
    if (enable) {
        _idleTimer = millis() + random(3000, 5000);
    }
}

// ============================================================================
// ANIMATIONS
// ============================================================================

void RobotEyes::playBlink() {
    if (_animPlaying || _sleepMode) return;
    _animPlaying = true;
    _anim.startTime = millis();
    _anim.duration = 200;
    _anim.phase = 0;
    _anim.progress = 0;
    _anim.loop = false;
    _dirty = true;
}

void RobotEyes::playDoubleBlink() {
    if (_animPlaying || _sleepMode) return;
    _animPlaying = true;
    _anim.startTime = millis();
    _anim.duration = 400;
    _anim.phase = 0;
    _anim.progress = 0;
    _anim.loop = false;
    _dirty = true;
}

void RobotEyes::playWinkLeft() {
    if (_animPlaying || _sleepMode) return;
    _animPlaying = true;
    _anim.startTime = millis();
    _anim.duration = 200;
    _anim.phase = 1; // Wink left phase
    _anim.progress = 0;
    _anim.loop = false;
    _left.targetEyelid = 0;
    _right.targetEyelid = 255;
    _dirty = true;
}

void RobotEyes::playWinkRight() {
    if (_animPlaying || _sleepMode) return;
    _animPlaying = true;
    _anim.startTime = millis();
    _anim.duration = 200;
    _anim.phase = 2; // Wink right phase
    _anim.progress = 0;
    _anim.loop = false;
    _left.targetEyelid = 255;
    _right.targetEyelid = 0;
    _dirty = true;
}

void RobotEyes::playStartup() {
    closeEyes();
    delay(100);
    openEyes();
    _dirty = true;
}

void RobotEyes::playShutdown() {
    closeEyes();
    _dirty = true;
}

bool RobotEyes::isAnimating() const {
    return _animPlaying;
}

bool RobotEyes::isSleeping() const {
    return _sleepMode;
}

uint16_t RobotEyes::getFPS() const {
    return _currentFps;
}

void RobotEyes::setEyeColor(uint16_t color) {
    _eyeColor = color;
    _dirty = true;
}

void RobotEyes::setPupilColor(uint16_t color) {
    _pupilColor = color;
    _dirty = true;
}

void RobotEyes::setFramerate(uint8_t fps) {
    _maxFps = constrain(fps, 20, 80);
    _frameInterval = 1000 / _maxFps;
}

uint8_t RobotEyes::getFramerate() const {
    return _maxFps;
}

void RobotEyes::openLeftEye() {
    _leftTarget.targetEyelid = 255;
    _leftTarget.isOpen = true;
    _dirty = true;
}

void RobotEyes::openRightEye() {
    _rightTarget.targetEyelid = 255;
    _rightTarget.isOpen = true;
    _dirty = true;
}

void RobotEyes::closeLeftEye() {
    _leftTarget.targetEyelid = 0;
    _leftTarget.isOpen = false;
    _dirty = true;
}

void RobotEyes::closeRightEye() {
    _rightTarget.targetEyelid = 0;
    _rightTarget.isOpen = false;
    _dirty = true;
}
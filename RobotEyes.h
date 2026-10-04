#ifndef RobotEyes_h
#define RobotEyes_h

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

enum EyeEmotion : uint8_t {
    EMOTION_NEUTRAL = 0,
    EMOTION_HAPPY,
    EMOTION_ANGRY,
    EMOTION_SAD,
    EMOTION_SLEEPY,
    EMOTION_SURPRISED,
    EMOTION_CURIOUS,
    EMOTION_SCARED,
    EMOTION_LOVE,
    EMOTION_LAUGHING,
    EMOTION_THINKING,
    EMOTION_WINK_LEFT,
    EMOTION_WINK_RIGHT
};

enum EyePosition : uint8_t {
    POS_CENTER = 0,
    POS_N,
    POS_NE,
    POS_E,
    POS_SE,
    POS_S,
    POS_SW,
    POS_W,
    POS_NW
};

class RobotEyes {
public:
    RobotEyes();
    ~RobotEyes();

    void begin(Adafruit_SSD1306* display, uint16_t screenWidth, uint16_t screenHeight);
    void begin(Adafruit_SSD1306* display, uint16_t screenWidth, uint16_t screenHeight, uint8_t maxFps);
    
    void update();
    void setFramerate(uint8_t fps);
    uint8_t getFramerate() const;
    
    void setEyeSize(uint8_t width, uint8_t height);
    void setBorderRadius(uint8_t radius);
    void setSpaceBetween(int16_t space);
    
    void setPosition(EyePosition pos);
    void setPupilOffset(int8_t x, int8_t y);
    
    void setEyelidOpen(uint8_t amount);
    void openEyes();
    void closeEyes();
    void setEyelids(uint8_t left, uint8_t right);
    void openLeftEye();
    void openRightEye();
    void closeLeftEye();
    void closeRightEye();
    
    void setPupilSize(uint8_t size);
    void setPupilPosition(int8_t x, int8_t y);
    void setPupils(int8_t lx, int8_t ly, int8_t rx, int8_t ry);
    
    void setEmotion(EyeEmotion emotion);
    void setEmotionWithTransition(EyeEmotion emotion, uint16_t transitionMs);
    EyeEmotion getEmotion() const;
    
    void setAutoblinker(bool enable);
    void setBreathing(bool enable);
    void setSleepMode(bool enable);
    void setIdleMovement(bool enable);
    
    void playBlink();
    void playDoubleBlink();
    void playWinkLeft();
    void playWinkRight();
    void playStartup();
    void playShutdown();
    
    bool isAnimating() const;
    bool isSleeping() const;
    uint16_t getFPS() const;
    
    void setEyeColor(uint16_t color);
    void setPupilColor(uint16_t color);
    
private:
    struct EyeState {
        uint8_t width;
        uint8_t height;
        uint8_t radius;
        int8_t pupilX;
        int8_t pupilY;
        uint8_t pupilSize;
        uint8_t eyelidOpen;
        uint8_t targetEyelid;
        bool isOpen;
    };
    
    struct EmotionConfig {
        uint8_t eyeWidth;
        uint8_t eyeHeight;
        uint8_t borderRadius;
        uint8_t pupilSize;
        int8_t pupilX;
        int8_t pupilY;
        uint8_t eyelidOpen;
        int8_t positionXOffset;
        int8_t positionYOffset;
    };
    
    struct AnimState {
        uint32_t startTime;
        uint32_t duration;
        float progress;
        bool active;
        bool loop;
        uint8_t phase;
    };
    
    void drawEyes();
    void drawEye(uint8_t index);
    void updateBehaviors();
    void updateAnimation();
    void updateEmotionTransition();
    void applyEmotion(EyeEmotion emotion);
    void calculatePositions();
    
    float easeInOut(float t);
    float easeOutBack(float t);
    
    void setupNeutral();
    void setupHappy();
    void setupAngry();
    void setupSad();
    void setupSleepy();
    void setupSurprised();
    void setupCurious();
    void setupScared();
    void setupLove();
    void setupLaughing();
    void setupThinking();
    void setupWinkLeft();
    void setupWinkRight();
    
    Adafruit_SSD1306* _display;
    
    uint16_t _screenW;
    uint16_t _screenH;
    uint8_t _maxFps;
    uint32_t _frameInterval;
    uint32_t _lastUpdate;
    uint16_t _currentFps;
    uint32_t _fpsCounter;
    uint32_t _fpsTimer;
    
    EyeState _left;
    EyeState _right;
    EyeState _leftTarget;
    EyeState _rightTarget;
    
    EmotionConfig _emotionConfigs[13];
    
    int16_t _spaceBetween;
    int8_t _posX;
    int8_t _posY;
    EyePosition _position;
    
    EyeEmotion _emotion;
    EyeEmotion _targetEmotion;
    bool _transitioning;
    uint32_t _transitionStart;
    uint32_t _transitionDuration;
    
    bool _autoBlink;
    uint32_t _blinkTimer;
    uint32_t _blinkCooldown;
    bool _breathing;
    uint32_t _breathTimer;
    float _breathPhase;
    bool _sleepMode;
    bool _idleMove;
    uint32_t _idleTimer;
    
    AnimState _anim;
    bool _animPlaying;
    
    uint16_t _eyeColor;
    uint16_t _pupilColor;
    
    bool _dirty;
    uint8_t _lastLeftEyelid;
    uint8_t _lastRightEyelid;
    int8_t _lastLeftPupilX;
    int8_t _lastLeftPupilY;
    int8_t _lastRightPupilX;
    int8_t _lastRightPupilY;
};

#endif
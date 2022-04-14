// USER PREFERENCES STRUCTURE
// ----------------
#define GROUP_COUNT  6

typedef struct 
{
  byte saved = 0;
  byte brightness = 130;
  byte colorMode = 0;
  byte rainbowTime = 192;
  byte hue = 0;
  byte saturation = 255;
  CRGB ledPreset[GROUP_COUNT];
} UserPreference;

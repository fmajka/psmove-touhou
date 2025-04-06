#include "external/Config-Parser-C/parser.h"
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <math.h>
#include <psmove.h>
#include <psmove_tracker.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Maps a value from a range to a new range
float mapf(float value, float old_min, float old_max, float new_min,
           float new_max) {
  return new_min +
         (value - old_min) * (new_max - new_min) / (old_max - old_min);
}

float clampf(float val, float min, float max) {
  return (val < min) ? min : (val > max) ? max : val;
}

// Emits a keyboard input event
void emit(int fd, int type, int code, int val) {
  struct input_event ie;
  memset(&ie, 0, sizeof(ie));
  ie.type = type;
  ie.code = code;
  ie.value = val;
  write(fd, &ie, sizeof(ie));
}

enum Arrow { ARROW_RIGHT = 0, ARROW_UP, ARROW_LEFT, ARROW_DOWN, ARROW_COUNT };

unsigned int ARROW_KEYCODES[] = {
    [ARROW_RIGHT] = KEY_RIGHT,
    [ARROW_UP] = KEY_UP,
    [ARROW_LEFT] = KEY_LEFT,
    [ARROW_DOWN] = KEY_DOWN,
};

// Sends events about changed arrow inputs compared to previous state
// Returns whether any input was actually changed
bool update_arrows(int fd, unsigned int prev_arrows, unsigned int new_arrows) {
  bool syn = false;
  unsigned int changed_arrows = new_arrows ^ prev_arrows;
  for (unsigned int i = 0, arrow = 0x1; i < ARROW_COUNT;
       i++, arrow = arrow << 1) {
    if (arrow & changed_arrows) {
      emit(fd, EV_KEY, ARROW_KEYCODES[i], (bool)(arrow & new_arrows));
      syn = true;
    }
  }
  return syn;
}

enum Entry {
  ADDR_X = 0,
  PRECISION_NORMAL,
  PRECISION_FOCUS,
  TH_MIN_X,
  TH_MAX_X,
  TH_MIN_Y,
  TH_MAX_Y,
  SHOT,
  BOMB,
  FOCUS,
  SKIP,
  PAUSE,
  ECODE_SHOT,
  ECODE_BOMB,
  ECODE_FOCUS,
  ECODE_SKIP,
  ECODE_PAUSE,
  CAM_MIN_X,
  CAM_MAX_X,
  CAM_MIN_Y,
  CAM_MAX_Y,
  SMART_SHOT,
  NAV_THRESHOLD_X,
  NAV_THRESHOLD_Y,
  ENTRY_COUNT,
};

typedef struct {
  char *key;
  int value;
} entry_t;

#define ECODE_ESC 1
#define ECODE_LEFTCTRL 29
#define ECODE_LEFTSHIFT 42
#define ECODE_Z 44
#define ECODE_X 45

enum SmartShot {
  SMART_SHOT_NONE = 0,
  SMART_SHOT_REVERSE,
  SMART_SHOT_TOGGLE,
  SMART_SHOT_COUNT,
};

entry_t config[ENTRY_COUNT] = {
    [ADDR_X] = {"addr_x"},
    [PRECISION_NORMAL] = {"precision_normal"},
    [PRECISION_FOCUS] = {"precision_focus"},
    [TH_MIN_X] = {"th_min_x"},
    [TH_MAX_X] = {"th_max_x"},
    [TH_MIN_Y] = {"th_min_y"},
    [TH_MAX_Y] = {"th_max_y"},
    [SHOT] = {"shot"},
    [BOMB] = {"bomb"},
    [FOCUS] = {"focus"},
    [SKIP] = {"skip"},
    [PAUSE] = {"pause"},
    [ECODE_SHOT] = {"ecode_shot", ECODE_Z},
    [ECODE_BOMB] = {"ecode_bomb", ECODE_X},
    [ECODE_FOCUS] = {"ecode_focus", ECODE_LEFTSHIFT},
    [ECODE_SKIP] = {"ecode_skip", ECODE_LEFTCTRL},
    [ECODE_PAUSE] = {"ecode_pause", ECODE_ESC},
    [CAM_MIN_X] = {"cam_min_x"},
    [CAM_MAX_X] = {"cam_max_x"},
    [CAM_MIN_Y] = {"cam_min_y"},
    [CAM_MAX_Y] = {"cam_max_y"},
    [SMART_SHOT] = {"smart_shot", SMART_SHOT_NONE},
    [NAV_THRESHOLD_X] = {"nav_threshold_x"},
    [NAV_THRESHOLD_Y] = {"nav_threshold_y"},
};
#define CONF(KEY) config[KEY].value

// Maps a string to a value
// Who needs hashtables anyway?
typedef struct {
  char *key;
  char **allowed;
} string_key_t;

typedef struct {
  char *string;
  int value;
} string_value_t;

char *SMART_SHOT_ALLOWED[] = {"NONE", "REVERSE", "TOGGLE", NULL};
char *ACTION_ALLOWED[] = {"TRIANGLE", "CIRCLE", "CROSS", "SQUARE", "SELECT",
                          "START",    "PS",     "MOVE",  "T",      NULL};

string_key_t STRING_KEYS[] = {
    {"smart_shot", SMART_SHOT_ALLOWED}, {"shot", ACTION_ALLOWED},
    {"bomb", ACTION_ALLOWED},           {"focus", ACTION_ALLOWED},
    {"skip", ACTION_ALLOWED},           {"pause", ACTION_ALLOWED},
};
size_t NUM_STRING_KEYS = sizeof(STRING_KEYS) / sizeof(STRING_KEYS[0]);

#define INIT_BTN(NAME) {#NAME, Btn_##NAME}
string_value_t STRING_VALUES[] = {
    {"NONE", SMART_SHOT_NONE},
    {"REVERSE", SMART_SHOT_REVERSE},
    {"TOGGLE", SMART_SHOT_TOGGLE},
    INIT_BTN(TRIANGLE),
    INIT_BTN(CIRCLE),
    INIT_BTN(CROSS),
    INIT_BTN(SQUARE),
    INIT_BTN(SELECT),
    INIT_BTN(START),
    INIT_BTN(PS),
    INIT_BTN(MOVE),
    INIT_BTN(T),
};
size_t NUM_STRING_VALUES = sizeof(STRING_VALUES) / sizeof(STRING_VALUES[0]);

// Hardcoded check for all possible string values
int get_string_value(char *key, char *value) {
  // Find the numeric value corresponding to the config string value
  for (int iStrVal = 0; iStrVal < NUM_STRING_VALUES; iStrVal++) {
    string_value_t sv = STRING_VALUES[iStrVal];
    if (strcmp(value, sv.string) == 0) {
      // Find the list of allowed values for the given key
      for (int iStrKey = 0; iStrKey < NUM_STRING_KEYS; iStrKey++) {
        string_key_t sk = STRING_KEYS[iStrKey];
        if (strcmp(key, sk.key) == 0) {
          // Check if the value is allowed for such key
          for (int iAllowed = 0; sk.allowed[iAllowed] != NULL; iAllowed++) {
            if (strcmp(value, sk.allowed[iAllowed]) == 0) {
              printf("For given [key, value] pair: [%s, %s] returning numeric "
                     "value %d\n",
                     key, value, STRING_VALUES[iStrVal].value);
              return STRING_VALUES[iStrVal].value;
            }
          }
          // That value was not allowed
          fprintf(stderr, "For key %s string value %s is not allowed\n", key,
                  value);
          return -1;
        }
      }
      // The given key wasn't found - weird
      fprintf(stderr, "Key %s not found within array for value %s\n", key,
              value);
      return -1;
    }
  }
  // No value for such string
  fprintf(stderr, "No value found for the string %s (key: %s)\n", value, key);
  return -1;
}

int parse_file(char *path) {
  config_option_t file;
  if ((file = read_config_file(path)) == NULL) {
    fprintf(stderr, "Error while trying to read `%s`\n", path);
    return 1;
  }
  printf("Reading file `%s`\n", path);
  // Fill file entries
  while (file != NULL) {
    for (int i = 0; i < ENTRY_COUNT; i++) {
      if (strcmp(file->key, config[i].key) == 0) {
        char *endptr;
        int num = strtol(file->value, &endptr, 0);
        // Either just set to the parsed the number or find the string value
        config[i].value =
            (*endptr == '\0') ? num : get_string_value(file->key, endptr);
        break;
      }
    }
    // printf("%s = %s\n", file->key, file->value);
    file = file->prev;
  }
  return 0;
}

int main(int argc, char **argv) {
  // Get game's process id and name
  int proc_id;
  char *proc_name;
  if (argc < 2) {
    fprintf(stderr, "Pass the game's process id/name as the first argument\n");
    return 1;
  } else {
    proc_id = strtol(argv[1], &proc_name, 0);
    char *cmd;
    if (*proc_name == '\0') {
      asprintf(&cmd, "ps -p %d -o comm=", proc_id);
    } else {
      asprintf(&cmd, "pgrep %s", proc_name);
    }
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
      fprintf(stderr, "Failed to get the process name/id\n");
      return 1;
    }
    free(cmd);
    char output[128];
    if (fgets(output, sizeof(output), fp) != NULL) {
      if (*proc_name == '\0') {
        // Trim trailing whitespace
        char *end = output + strlen(output) - 1;
        while (end > output && isspace((unsigned char)*end)) {
          end--;
        }
        end[1] = '\0';
        proc_name = output;
      } else {
        proc_id = strtol(output, NULL, 0);
      }
    }
    pclose(fp);
  }
  if (proc_id == 0 || strlen(proc_name) == 0) {
    fprintf(stderr, "Process id: %d and name: %s not initialized properly\n",
            proc_id, proc_name);
    return 1;
  }

  printf("Process id: %d, process name: %s (string length: %ld)\n", proc_id,
         proc_name, strlen(proc_name));

  // PSMove init
  PSMove *move = psmove_connect();
  if (!move) {
    fprintf(stderr, "Could not connect to PSMove controller.\n");
    return 1;
  }

  psmove_enable_orientation(move, true);
  assert(psmove_has_orientation(move));

  PSMoveTracker *tracker = psmove_tracker_new();
  if (!tracker) {
    fprintf(stderr, "Could not initialize PSMove tracker.\n");
    return 1;
  }

  // Set the default camera bounds, will be overwritten if set in any config
  // file
  int tracker_w, tracker_h;
  psmove_tracker_get_size(tracker, &tracker_w, &tracker_h);
  config[CAM_MAX_X].value = tracker_w;
  config[CAM_MAX_Y].value = tracker_h;

// Config init
#define CONFIG_PATH "./config/"
#define CONFIG_SUFFIX ".cfg"
  parse_file(CONFIG_PATH "default" CONFIG_SUFFIX);
  // Get the game name (strip exe) to get the config file name
  int game_len = 0, proc_len = strlen(proc_name);
  for (; game_len < proc_len && proc_name[game_len] != '.'; game_len++)
    ;
  char game_name[game_len + 1];
  memset(game_name, 0, game_len + 1);
  memcpy(game_name, proc_name, game_len);
  char *game_config_path;
  asprintf(&game_config_path, "%s%s%s", CONFIG_PATH, game_name, CONFIG_SUFFIX);
  parse_file(game_config_path);
  // Print entry info
  printf("Parsed config files...\n");
  for (int i = 0; i < ENTRY_COUNT; i++) {
    printf("%s = %d\n", config[i].key, config[i].value);
  }

  // Keyboard input emulation setup
  // https://kernel.org/doc/html/v4.19/input/uinput.html
  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    perror("Error trying to open uinput");
    return 1;
  }
  // Enable events for used keys
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  for (int keycode = ECODE_SHOT; keycode <= ECODE_PAUSE; keycode++) {
    ioctl(fd, UI_SET_KEYBIT, CONF(keycode));
  }
  for (int keycode = 0; keycode < ARROW_COUNT; keycode++) {
    ioctl(fd, UI_SET_KEYBIT, ARROW_KEYCODES[keycode]);
  }
  // Sample device setup
  struct uinput_setup usetup;
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.bustype = 0x1234;
  usetup.id.product = 0x5678;
  strcpy(usetup.name, "Example device");
  ioctl(fd, UI_DEV_SETUP, &usetup);
  ioctl(fd, UI_DEV_CREATE);
  sleep(1);

  while (psmove_tracker_enable(tracker, move) != Tracker_CALIBRATED) {
    printf("Tring to calibrate tracker...\n");
  }

  unsigned int prev_arrows = 0;
  unsigned int prev_buttons = 0;
  bool nav_mode = true;
  // TODO: nav values now range from 0 to 100, should be 0 to 1.0
  float nav_x = CONF(NAV_THRESHOLD_X) / 100.f;
  float nav_y = CONF(NAV_THRESHOLD_Y) / 100.f;

  while (1) {
    psmove_tracker_update_image(tracker);
    psmove_tracker_update(tracker, NULL);

    while (psmove_poll(move))
      ;

    // Get controller data
    float move_x, move_y;
    psmove_tracker_get_position(tracker, move, &move_x, &move_y, NULL);
    unsigned int move_buttons = psmove_get_buttons(move);
    float ax, ay, az;
    psmove_get_accelerometer_frame(move, Frame_SecondHalf, &ax, &ay, &az);

    // End the program is the PS button was pressed
    if (move_buttons & Btn_PS) {
      printf("PS Button pressed, playtime is over!\n");
      break;
    }

    // Sync uinput if true
    bool syn = false;

    // Toggle nav mode
    if (~prev_buttons & move_buttons & Btn_START) {
      nav_mode = !nav_mode;
      printf("Navigation mode set to: %d\n", nav_mode);
      // Loop through arrow bits and release them
      if (update_arrows(fd, prev_arrows, 0)) {
        syn = true;
      }
      prev_arrows = 0;
    }

    // Read player's position from game memory
    char *proc_path;
    asprintf(&proc_path, "/proc/%d/mem", proc_id);
    int fp = open(proc_path, O_RDONLY);
    lseek(fp, CONF(ADDR_X), SEEK_SET);
    float pos[2];
    int bytes_read = read(fp, pos, sizeof(pos));
    float game_x = pos[0], game_y = pos[1];
    close(fp);

    // Clamp the move controller's position to the camera's min/max
    move_x = clampf(move_x, CONF(CAM_MIN_X), CONF(CAM_MAX_X));
    move_y = clampf(move_y, CONF(CAM_MIN_Y), CONF(CAM_MAX_Y));
    // Map the controller's coordiantes relatively to the camera
    // to the target in-game coordiantes
    float target_x = mapf(move_x, CONF(CAM_MIN_X), CONF(CAM_MAX_X),
                          CONF(TH_MIN_X), CONF(TH_MAX_X));
    float target_y = mapf(move_y, CONF(CAM_MIN_Y), CONF(CAM_MAX_Y),
                          CONF(TH_MIN_Y), CONF(TH_MAX_Y));
    // Mirror target_x
    target_x = CONF(TH_MAX_X) + CONF(TH_MIN_X) - target_x;

    float dx = target_x - game_x, dy = target_y - game_y;
    float angle = atan2f(-dy, dx);
    // Map the angle to a value in range [0; 2PI]
    angle = fmodf(angle + 2 * M_PI, 2 * M_PI);
    // printf("Game: %f, %f | Move: %f, %f | Target: %f, %f | Angle: %f\n",
    // game_x, game_y, move_x, move_y, target_x, target_y, angle / M_PI *
    // 180.f);

    // Menu navigation with controller rotation and the move button
    // TODO: allow the user to assign the nav movement button in config
    if (nav_mode && (prev_buttons ^ move_buttons & Btn_MOVE)) {
      unsigned int new_arrows = 0;
      // Get the correct arrow if pressed, if released - reset
      if(move_buttons & Btn_MOVE) {
        int arrow_index = (ay > nav_y) ? ARROW_UP : ARROW_DOWN;
        arrow_index = (ax > nav_x) ? ARROW_LEFT : (ax < -nav_x) ? ARROW_RIGHT : arrow_index;
        new_arrows = 0x1 << arrow_index;
      }
      if(update_arrows(fd, prev_arrows, new_arrows)) {
        syn = true;
      }
      printf("Acc: %.2f, %.2f | Threshold: %.2f, %.2f\n", ax, ay, nav_x, nav_y);
      printf("Nav new arrows: %x\n", new_arrows);
      prev_arrows = new_arrows;
    }

    // Player character movement
    if(!nav_mode) {
      float dist = sqrtf(dx * dx + dy * dy);
      bool focus = move_buttons & CONF(FOCUS);
      float prec = (focus) ? CONF(PRECISION_FOCUS) : CONF(PRECISION_NORMAL);
      // Check distance from target and decide what to do
      if (dist <= prec) {
        // printf("Close enough\n");
        //  We're close enough, depress all arrow keys to stop moving
        if (update_arrows(fd, prev_arrows, 0)) {
          syn = true;
        }
        prev_arrows = 0;
      } else {
        int key_index = (int)(angle / M_PI_4 + 0.5f) % 8;
        int i = key_index / 2;
        unsigned int new_arrows =
            (key_index % 2 == 0) ? 0x1 << i : 0x1 << i | 0x1 << (i + 1) % 4;
        if (update_arrows(fd, prev_arrows, new_arrows)) {
          syn = true;
        }
        // printf("Arrows: %x => %x\n", prev_arrows, new_arrows);
        prev_arrows = new_arrows;
      }
    }

    // Handle button presses
    unsigned int changed_buttons = move_buttons ^ prev_buttons;
    if (changed_buttons) {
      // Loop through all actions
      for (int action_id = SHOT; action_id <= PAUSE; action_id++) {
        unsigned int button = CONF(action_id);
        if (!(button & changed_buttons)) {
          continue;
        }
        bool keyval = button & move_buttons;
        // printf("Nav: %d\n", nav_mode);
        //  Smart shot expections
        int smart_shot = CONF(SMART_SHOT);
        if (!nav_mode && action_id == SHOT && smart_shot != SMART_SHOT_NONE) {
          if (smart_shot == SMART_SHOT_REVERSE) {
            keyval = !keyval;
          } else if (smart_shot == SMART_SHOT_TOGGLE) {
            if (!keyval) {
              continue;
            }
            keyval = (bool)!(button & prev_buttons);
          }
        }
        // TODO: Some better way of getting the action keycode
        emit(fd, EV_KEY, CONF(action_id + 5), keyval);
        syn = true;
      }
      prev_buttons = move_buttons;
    }
    if (syn) {
      emit(fd, EV_SYN, SYN_REPORT, 0);
    }
  }

  psmove_disconnect(move);
  psmove_tracker_free(tracker);
  ioctl(fd, UI_DEV_DESTROY);
  close(fd);

  return 0;
}

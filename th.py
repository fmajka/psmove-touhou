import argparse
import configparser
import math
import os
import psmoveapi
import struct
import subprocess
import sys
from evdev import UInput, ecodes as e

# Helper map function
def map_value(value, old_min, old_max, new_min, new_max):
    return new_min + (value - old_min) * (new_max - new_min) / (old_max - old_min)

# Parse command line arguments
parser = argparse.ArgumentParser()
default_path = "default.cfg"
parser.add_argument("proc", help="Game process id/name", type=str)
parser.add_argument("-c", "--config", 
                    help=f"Path to the config file, default: '{default_path}'", 
                    type=str, 
                    default=default_path)
args = parser.parse_args()

# Parse config file
config_path = args.config
if not os.path.exists(config_path):
    print(f"Error: couldn't find the config file: '{config_path}'")
    exit(1)
config = configparser.ConfigParser()
config.read(config_path)

# Let's start the party!
proc = args.proc
try:
    pid = int(proc if proc.isdigit() else subprocess.check_output(["pgrep", proc]))
except Exception as err:
    print(f"Error while trying to get pid for '{proc}': {err}")
    exit(1)

# Parse config file
try:
    THCONF = config['touhou']
    TH_ADDR_X = int(THCONF["addr_x"], 16)
    TH_PREC_N = int(THCONF["precision_normal"])
    TH_PREC_F = int(THCONF["precision_focus"])
    TH_MIN_X = int(THCONF["min_x"])
    TH_MAX_X = int(THCONF["max_x"])
    TH_MIN_Y = int(THCONF["min_y"])
    TH_MAX_Y = int(THCONF["max_y"])
    TH_SMART_SHOT = THCONF['smart_shot'] if "smart_shot" in THCONF else "NONE"
    if TH_SMART_SHOT not in ["TOGGLE", "REVERSE", "NONE"]:
        raise Exception("Supported 'smart_shot' values: TOGGLE, REVERSE, NONE")
    if TH_SMART_SHOT == "NONE":
        TH_SMART_SHOT = False
    PSMCONF = config['psmove']
    PSM_MIN_X = int(PSMCONF["min_x"])
    PSM_MAX_X = int(PSMCONF["max_x"])
    PSM_MIN_Y = int(PSMCONF["min_y"])
    PSM_MAX_Y = int(PSMCONF["max_y"])
except Exception as err:
    print("Config parse error:", err)
    exit(1)

# Touhou keymaps
ARROW_KEYCODES = [
    set([e.KEY_RIGHT]),
    set([e.KEY_UP, e.KEY_RIGHT]),
    set([e.KEY_UP]),
    set([e.KEY_UP, e.KEY_LEFT]),
    set([e.KEY_LEFT]),
    set([e.KEY_DOWN, e.KEY_LEFT]),
    set([e.KEY_DOWN]),
    set([e.KEY_DOWN, e.KEY_RIGHT]),
]
BUTTON_KEYCODES = {
    "shot": e.KEY_Z,
    "bomb": e.KEY_X,
    "focus": e.KEY_LEFTSHIFT,
    "skip": e.KEY_LEFTCTRL,
    "pause": e.KEY_ESC,
}

# Keyboard ecode overrides
for key in BUTTON_KEYCODES:
    attr = f"ecode_{key}"
    if attr in THCONF:
        try:
            BUTTON_KEYCODES[key] = int(THCONF[attr])
        except Exception as err:
            print(f"Error while assigning ecode from config: {err}")
            exit(1)

# Check keymap config file
try:
    for key in BUTTON_KEYCODES.keys():
        getattr(psmoveapi.Button, THCONF[key])
except Exception as err:
    print(f"Error while checking button keymaps: {err}")
    exit(1)

# Tracks pressed buttons
pressed_arrows = set()
pressed_buttons = 0
pressed_trigger = 0

# Virtual input device for keyboard input simulation
ui = UInput()

# Navigation mode
nav_mode = True

while True:
    # Read psmove data
    line = sys.stdin.readline().strip()

    # Parse PSMove data
    data = line.split()
    signal = data[0]
    if signal == "PS":
        print("Received playtime over signal, it's so over")
        exit(0)
    elif signal == "update":
        psmove_x = float(data[1])
        psmove_y = float(data[2])
        psmove_buttons = int(data[3])
        psmove_trigger = int(data[4])
        #print(f"PSMove: {psmove_x:.2f}, {psmove_y:.2f}, {psmove_buttons}, {psmove_trigger}")
    else:
        print("Received unknown signal:", signal)
        continue

    # Sync if key state changed
    syn = False

    # Nav mode - don't process inputs
    print(f"Nav mode: {nav_mode}")
    if (pressed_buttons ^ psmove_buttons) & psmoveapi.Button.START:
        if psmove_buttons & psmoveapi.Button.START:
            nav_mode = not nav_mode
            # Reset arrow key state when going into nav mode
            if nav_mode and pressed_arrows:
                for keycode in pressed_arrows:
                    ui.write(e.EV_KEY, keycode, 0)
                pressed_arrows = set()
                syn = True

    # Read character position from memory
    try:
        fd = open(f"/proc/{pid}/mem", "rb")
        fd.seek(TH_ADDR_X)
        data = fd.read(8) 
        touhou_x, touhou_y = struct.unpack('<ff', data)
    except Exception as err:
        print(f"Error while trying to open or read process {pid}:", err)
        continue

    # Map psmove coordinates to touhou coordinates
    target_x = map_value(psmove_x, PSM_MIN_X, PSM_MAX_X, TH_MIN_X, TH_MAX_X)
    target_y = map_value(psmove_y, PSM_MIN_Y, PSM_MAX_Y, TH_MIN_Y, TH_MAX_Y)

    # Mirror x
    target_x = (TH_MAX_X + TH_MIN_X) - target_x
    #print("{:.2f}, {:.2f}, {:.2f}, {:.2f}".format(touhou_x, touhou_y, target_x, target_y))

    # Get the distance diff and angle in which the character should move
    dx, dy = target_x - touhou_x, target_y - touhou_y
    angle = math.degrees(math.atan2(-dy, dx))
    angle = (angle + 360) % (360)

    # Handling movement
    if not nav_mode:
        distance = math.sqrt(dx * dx + dy * dy)
        #print(f"Distance: {distance}, angle: {angle}")
        # Check if in focus mode
        focus = psmove_buttons & getattr(psmoveapi.Button, THCONF['focus'])
        prec = TH_PREC_F if focus else TH_PREC_N
        if distance <= prec:
            # Distance check - don't move if close enough
            for keycode in pressed_arrows:
                ui.write(e.EV_KEY, keycode, 0)
                syn = True
            pressed_arrows = set()
        else:
            # Check the angle between psmove pos and touhou pos
            key_index = round(angle / 45) % 8

            # Handle movement
            new_arrows = ARROW_KEYCODES[key_index]
            changed = pressed_arrows ^ new_arrows
            for keycode in changed:
                ui.write(e.EV_KEY, keycode, int(keycode in new_arrows))
                syn = True
            pressed_arrows = new_arrows

    # Handle buttons
    if psmove_buttons != pressed_buttons:
        changed_buttons = psmove_buttons ^ pressed_buttons 
        for action, keycode in BUTTON_KEYCODES.items(): 
            button = getattr(psmoveapi.Button, THCONF[action]) 
            if button & changed_buttons:
                keyval = button & psmove_buttons
                # Smart shot exceptions
                if not nav_mode and action == "shot" and TH_SMART_SHOT:
                    if TH_SMART_SHOT == "REVERSE":
                        keyval = int(not keyval)
                    if TH_SMART_SHOT == "TOGGLE":
                        if not button & psmove_buttons:
                            continue
                        keyval = int(not (button & pressed_buttons))
                # Write
                ui.write(e.EV_KEY, keycode, keyval)
                syn = True
        pressed_buttons = psmove_buttons

    # Update
    if syn:
        ui.syn()


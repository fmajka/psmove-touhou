## Introduction
Recently I decided to mess around a little bit with the PlayStation Move controllers. I thought that fun experiment would be to try to somehow get them to work with the Touhou games. After some adjustments and troubleshooting it turned out to work surprisingly well, at least for my standards~

## Disclaimer
This is a work in progress and for now there are some limitations:
* The program for now only works on Linux (it's using some Linux-specific headers). I will try to make it work on Windows once I am done with all the features.
* There are still some manual adjustments one needs to make to get this work properly. I will try to list them all here. In the future updates I will try to automate this process as much as possible.

## How it works

### General idea
The entire idea is based around reading the position of the player character from the game process memory. One can manually locate the memory adress by using memory scanner software like GameConqueror (on Linux) or CheatEngine (on Windows). The extracted memory adress, along with some other configuration, like keybindings, is read from the config file when the program starts. It reads the player's position data from the game's memory and compares it with the position of the PSMove contoller relative to the camera. The program then emulates keyboard inputs via uinput in order to move the character so that the positions match.

### Components
The entire program consists of one executable file: thpsm, which parses the config file, initializes all the tools and emulates input

There are also config files that allow you to setup the game the way you want, allowing you to adjust the keybinds, precision and play area, along with some optional quallity of life settigns. The details are explained within the `default.cfg` file through comments.

## Prerequisites
Hardware requirements:
* A PlayStation Move controller
* A PlayStation Eye camera (or perhaps any webcam will do)

Software requirements:
* Wine or Proton (to run Touhou on Linux)
* GameConqueror (for finding the player's position memory adress, one time setup)
* PSMoveAPI (for getting the PSMove controllers to work)
* (optional) protontricks (to easily get the steam IDs of the games)

## Building and running
* Build the psmove_tracker program by running `./build.sh`.
* You might want to check out the [Troubleshooting](#troubleshooting) section in order to prevent any issues that might occur.
* Now you can run the program using `./run.sh XXXXX`, passing the game's pid or name as XXXXX. In my case I pass `th07e.exe` as I'm running Touhou 7. Make sure to run the game BEFORE you run the program, else it will fail.
* Once you've started the game, you can move around the menus by pressing the MOVE button and rotating the controller left/right or up/down. Press the START button once you enter the stage to toggle movement tracking.

## Troubleshooting
Some issues I faced along the way. I will try to add more info and maybe a script for automating the process as I test the games more thoroughly.

### Reading the memory
You will have to make sure the program has access to the game's memory. For now I am just running it with sudo but maybe you will find a better way.

### Hiding controller input from Touhou
The game will see and recognize the controller's inputs by default but that's acutally a bad thing as there are issues with those (like the trigger being recognized as the X axis of the left stick). In order to fix those problems you need to hide the controller so that the game doesn't see it.

How I did it:
* Add the game through Steam (skip if it's a Steam game already)
* Find the game AppID, you can conveniently use `protontricks -s touhou`
* To disable the controler you can use `WINEPREFIX="~/.steam/steam/steamapps/compatdata/XXXXX/pfx" wine control`, replacing XXXXX with game game's AppID

## Feature roadmap
- [x] Actually playing the game with the controller
- [x] Overriding default keyboard bindings
- [x] Temporarily disabling position processing with the START button (makes navigating menus less of a pain)
- [x] "Smart shot" quality of life setting
- [x] File reorganization
- [x] Cascading config files (gamename.cfg -> default.cfg)
- [x] Navigation mode (navigating the menus with the PSMove controller)
- [x] Full C rewrite (previously a Python script)
- [ ] Reimplement additional config file passing
- [ ] Windows support
- [ ] Better documentation (dependencies and setup consolidated)
- [ ] Setup script (automating inital setup and troubleshooting)
- [ ] GUI overlay (browsing and running games from the desktop with the controller)

 #!/usr/bin/bash 
# gcc -o psmove_tracker psmove_tracker.c -I/usr/include/psmoveapi -L/usr/local/lib -lpsmoveapi -lpsmoveapi_tracker `pkg-config --cflags --libs opencv4`

gcc -o psmove_tracker psmove_tracker.c -I/usr/include/psmoveapi -lpsmoveapi -lpsmoveapi_tracker


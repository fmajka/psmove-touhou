#include <stdio.h>
#include <stdlib.h>
#include <psmove.h>
#include <psmove_tracker.h>
#include <assert.h>

int main() {
    PSMove *move = psmove_connect();
    if(!move) {
        fprintf(stderr, "Could not connect to PSMove controller.\n");
        return 1;
    }

    PSMoveTracker *tracker = psmove_tracker_new();
    if(!tracker) {
        fprintf(stderr, "Could not initialize PSMove tracker.\n");
        return 1;
    }

    psmove_enable_orientation(move, true);
    assert(psmove_has_orientation(move));

    while(psmove_tracker_enable(tracker, move) != Tracker_CALIBRATED) {
        printf("Tring to calibrate tracker...\n");
    }

    while(1) {
        psmove_tracker_update_image(tracker);
        psmove_tracker_update(tracker, NULL);

        while(psmove_poll(move));

        if (psmove_get_buttons(move) & Btn_PS) {
            printf("PS Button pressed, playtime is over!\n");
            fflush(stdout);
            break;
        }

        float x, y, radius;
        psmove_tracker_get_position(tracker, move, &x, &y, &radius);
        unsigned int buttons = psmove_get_buttons(move);
        unsigned char trigger = psmove_get_trigger(move);

        printf("update %.2f %.2f %u %d\n", x, y, buttons, trigger);
        fflush(stdout);
    }

    psmove_disconnect(move);
    psmove_tracker_free(tracker);
    return 0;
}

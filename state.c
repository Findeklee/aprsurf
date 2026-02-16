#include "state.h"
#include "menu.h"
#include "wall.h"


void switch_to_menu(Session *s) {
    s->handler = handle_menu;
    s->state_changed = 1;
}

void switch_to_wall(Session *s) {
    s->handler = handle_wall;
    s->state_changed = 1;
}

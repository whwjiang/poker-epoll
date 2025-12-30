#pragma once

#include "player.h"
#include "table.h"

struct Connection {
  int fd;
  poker::Player *p;
  poker::Table *table;
};

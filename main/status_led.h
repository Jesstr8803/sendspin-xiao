#pragma once

#include "sendspin/client.h"

void status_led_start(sendspin::SendspinClient& client, int gpio, bool active_low);

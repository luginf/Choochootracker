#include "doctest.h"
#include "meter_display.h"
#include <cmath>
TEST_CASE("Track meter has a bounded logarithmic response") {
  CHECK(monitorMeterHeight(0, 24) == 0);
  CHECK(monitorMeterHeight(-1, 24) == 0);
  CHECK(monitorMeterHeight(NAN, 24) == 0);
  CHECK(monitorMeterHeight(0.001f, 24) == 0);
  CHECK(monitorMeterHeight(0.1f, 24) == 14);
  CHECK(monitorMeterHeight(1, 24) == 24);
  CHECK(monitorMeterHeight(100, 24) == 24);
}

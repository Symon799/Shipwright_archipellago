#include "check.h"

int main() {
    CHECK(false); // CTest expects failure: detects checks accidentally disabled
                  // in Release.
}

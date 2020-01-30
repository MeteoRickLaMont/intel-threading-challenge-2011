#include <stdint.h>             // For uint32_t

//
// powertable contains all perfect powers N over the range
// [gFirstPower, gLastPower] along with a pair (base, exp) such that
// base^exp = N. When more than one such pair exists, it contains the one
// that minimizes the value of exp. BTW, the minimum value of exp will
// always be prime. This is a property of perfect powers.
//

//
// The highest prime exponent that's representable as a sum of consecutive
// 32-bit primes is 41:
// sum(2189273:8555623) = 2199023255552 = 2**41
//
// The highest perfect power that's representable as a sum of consecutive
// 32-bit primes is 425643382811212036:
// sum(4183177:4294937431) = 425643382811212036 = 652413506**2
//
const uint32_t EXPONENT_MAX = 41;
const tPower POWER_MAX = 425643382811212036ul;
const uint32_t POWERTABLE_MAX = 653168523;     // 8 through POWER_MAX

class PerfectPower {
public:
    PerfectPower() {}
    PerfectPower(uint32_t x, uint32_t k);

    tPower n;                   // n is a perfect power
    uint32_t base, exp;         // n = base ^ exp

    //
    // Choose minimum perfect power.
    // If two are equal, choose the one with the smaller exponent.
    // That's how to resolve duplicates in power table.
    //
    bool operator<(const PerfectPower &rhs) const {
        return n == rhs.n ? exp < rhs.exp : n < rhs.n;
    }

    void incrBase();            // increment base and recalculate n
};

//
// Globals
//
extern uint32_t gMaxPower;              // Command-line input
extern tPower gFirstPower;
extern volatile tPower gLastPower;      // Prime thread may lower this
extern PerfectPower *gPowerTable;
extern volatile const PerfectPower *gPowerEnd;

//
// Exported functions
//
uint32_t buildPowerTable(PerfectPower *&powertable);
extern const PerfectPower *findpower(const tPower p, const PerfectPower *a,
    const uint32_t n, const int32_t bias);

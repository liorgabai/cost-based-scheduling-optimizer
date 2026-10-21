# Test Cases

The project was compiled with:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic
```

The following integration scenarios were exercised.

## 1. ONE_TO_ONE minimum-cost compromise

Input:
`examples/one_to_one.csv`

Expected:
- no zero-cost schedule
- minimum Damage Cost = 4
- Maya is proposed for 22:00-23:00
- Noa's HARD 22:00 assignment is never selected

Observed:
PASS

## 2. WORKFORCE minimum-cost flow

Input:
`examples/workforce.csv`

Expected:
- all five appointments covered
- one employee can serve multiple different slots
- no employee serves two appointments in the same slot
- Noa's HARD slot is never selected
- minimum Damage Cost = 4

Observed:
PASS

## 3. UNKNOWN validation

Input:
`tests/invalid_unknown.csv`

Expected:
- optimization does not run
- input validation fails and asks for manual review

Observed:
PASS

## 4. Multi-change manager feedback

Input:
`tests/multi_change.csv`

Round 1:
- two required moves are shown in increasing severity order
- manager reports the second move failed

Expected state update:
- first move becomes AVAILABLE
- failed move becomes HARD
- later moves, if any, remain unchanged
- optimizer runs again globally

Observed:
PASS

## 5. Persisted state reload

After accepting a soft move, the generated state CSV was reopened.

Expected:
- confirmed move is now AVAILABLE
- optimizer can produce a zero-cost schedule from the updated knowledge

Observed:
PASS

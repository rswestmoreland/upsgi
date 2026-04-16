# B2c Revalidation / Hardening Note

## What was revalidated
- fresh single-threaded rebuild of the B2c tree with `CPUCOUNT=1`
- scheduler regression coverage
- retained W5-adjacent static regression coverage
- retained upload regression coverage

## What we found
A fresh rebuilt B2c binary preserved the important scheduler signals:
- bytes, completion, promotion, and capped-budget accounting all worked
- static and upload regressions still passed

One scheduler telemetry expectation was too strict for cross-host validation:
- `body_sched_full_budget_turns` remained monotonic
- but it did not always increment for a single local upload on this host
- the cause is local read segmentation: capped grants still occurred, but the read size observed by the server did not always satisfy the full-budget ratio test on every run

This means `body_sched_full_budget_turns` is useful telemetry, but it is not a portable correctness gate for a single rebuilt-local upload test.

## Hardening applied
- scheduler regression assertions were relaxed so they still verify the scheduler path, without requiring a nonzero `body_sched_full_budget_turns` sample from every local host/kernel/socket combination
- the stronger correctness gates remain:
  - bytes accounted
  - completion accounted
  - bulk promotion observed
  - capped-budget turns observed through `body_sched_no_credit_skips`
  - static and upload regressions remain green

## Resulting posture
- B2c remains the scheduler candidate baseline
- the fresh rebuild did not reveal a reason to discard the B2c line
- the scheduler workstream can now move forward without treating `body_sched_full_budget_turns > 0` as a universal local-host invariant

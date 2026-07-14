## Change

Describe the behavior and the safety boundary affected by this change.

## Evidence

- [ ] Focused tests pass.
- [ ] `bash scripts/verify_release.sh` passes when release-facing behavior changes.
- [ ] README and project claims distinguish implemented work from plans.
- [ ] No API keys, tokens, private keys, build artifacts, or local paths are committed.

## Safety Review

- Does this change allow a model to provide coordinates, velocity, paths,
  retry counts, or recovery policy?
- Can an invalid request create a Nav2 Goal before Guard approval?
- Are timeout, cancellation, and failure paths still bounded?

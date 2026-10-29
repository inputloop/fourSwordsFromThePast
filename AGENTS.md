# To any agent working in this repository

<!-- landing-rule 1.0: where work lands. Installed by ld1ck (c4/skills-mint/2026-10-29/landing); edit the source and run `ld1ck install` again, never this block -->
## Where work lands

- Work lands on this repository's default branch (`main`). A session does not invent a branch name, and
  does not invent a reason not to work on `main`.
- A branch a hosting harness assigns is a staging post, not a destination: when this repository's checks are
  green, fast-forward `main` to it and push both in the same turn. A branch is opened for review only where
  the line at the end of this block says `landing: review-first`, or where the person asks for one.
- Check, then re-check: run the repository's checks before the push, and again after it on what actually
  landed (the fetched `main`); keep both receipts.
- The hand-back says, for every repository pushed, where the work can be seen: the repository, `main` and
  whether it moved, the commit. Anywhere in the hand-back, in any order: `ld1ck handback` reads it as a
  container, never as a template.
- Nothing here lets a session make a repository public, rewrite history, force-push, or move a partner's
  `main`: in a partner's repository, proposals only, on a branch they can take or leave.

landing: main
<!-- /landing-rule -->

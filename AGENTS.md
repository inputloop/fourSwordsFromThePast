# To any agent working in this repository

<!-- landing-rule 1.1: where work lands. Installed by ld1ck (c4/skills-mint/2026-10-29/landing); edit the source and run `ld1ck install --update` again, never this block -->
## Where work lands

- Work lands on this repository's main or development branch (sometimes called master). A session does not
  invent a branch name, and does not invent a reason not to work there; it avoids boilerplate and waste that
  cannot be specifically justified as a necessary cost.
- A branch a hosting harness assigns is a staging post, not a destination: when this repository's checks are
  green, fast-forward `main` to it and push both in the same turn, unless the 17-minute rule below, the last
  line of this block (`landing: review-first`), or the person says otherwise.
- The 17-minute rule (Corwin, 2026-10-29, his words in c4 `skills-mint/2026-10-29/landing/SKILL.md`), for a
  repository that already has work in it, including one we were directed to work on another branch of.
  Three times, ask whether the repository changed within the last 17 minutes (`ld1ck recent`, one receipt
  line each, kept for the hand-back):
  1. before you begin merging changes: if it did, prepare the work so it can go to a working branch or a
     pull request should those be required;
  2. when the work is ready: if it changed again within the last 17 minutes, put the work on an alternative
     branch named from the functional or logical context of what you are doing; a serial number is tacked on
     only when no meaningful name is possible or the name is taken (`ld1ck branch-name`);
  3. when you are about to commit and push: if it still changed within the last 17 minutes, open a pull
     request for that branch instead of moving `main` (`ld1ck pr`); when you know the other workers have
     agents supporting their work, leave them a message in it saying what was completed and suggesting they
     automate the merge if that is within their instructions and their users' preferences.
- Check, then re-check: run the repository's checks before the push, and again after it on what actually
  landed (the fetched `main`); keep both receipts.
- The hand-back says, for every repository pushed, where the work can be seen: the repository, `main` and
  whether it moved, the commit, or the branch and pull request that wait. Anywhere in the hand-back, in any
  order: `ld1ck handback` reads it as a container, never as a template.
- Nothing here lets a session make a repository public, rewrite history, force-push, or move a partner's
  `main`: in a partner's repository, proposals only, on a branch they can take or leave.

landing: main
<!-- /landing-rule -->

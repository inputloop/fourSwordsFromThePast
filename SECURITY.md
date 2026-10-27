# Reporting a security or privacy problem

Four Swords of the Past opens network ports when you host, downloads a host's
game when you join, and talks to Archipelago rooms. If you find a way for a
stranger to crash a game, read something they should not, or run code on a
player's computer, please tell us privately first:

- GitHub's private vulnerability reporting for this repository (the Security
  tab, "Report a vulnerability"), if it is enabled; otherwise
- mail BryantAndCorwin@inputloop.com with "fourSwordsFromThePast" in the subject.

Say what you did, what you saw, and which build (the commit or the artifact
name). A packet capture or a small harness is welcome; a room code, a player's
address or a save file with someone's game in it is not needed and better left
out. Please do not put addresses or room codes in a public issue.

We answer within a week with what we understood and what happens next. Fixes
land in the repository with a line in `HARDEN.md`, which records every finding
we know of, open or closed, with the test that would prove it wrong.

What the game sends and stores is written in `MULTIPLAYER.md`, under "What
leaves your computer, and what arrives".

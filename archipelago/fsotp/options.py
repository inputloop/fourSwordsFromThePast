from dataclasses import dataclass

from Options import Choice, DeathLink, PerGameCommonOptions


class Goal(Choice):
    """What finishes your game. For now there is one goal: beat Ganon."""
    display_name = "Goal"
    option_ganon = 0
    default = 0


@dataclass
class FSotPOptions(PerGameCommonOptions):
    goal: Goal
    death_link: DeathLink

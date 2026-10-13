from BaseClasses import CollectionState
from test.bases import WorldTestBase

from ..items import GAME


class FSotPTestBase(WorldTestBase):
    game = GAME

    def state_with(self, items: dict[str, int]) -> CollectionState:
        """A state with exactly these items (name -> count) and nothing else."""
        state = CollectionState(self.multiworld)
        for name, n in items.items():
            for _ in range(n):
                state.collect(self.world.create_item(name), prevent_sweep=True)
        return state

    def reachable(self, location: str, items: dict[str, int]) -> bool:
        return self.world.get_location(location).can_reach(self.state_with(items))

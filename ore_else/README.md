# Ore Else

You crash-landed on a hostile planet with a supply crate and a pistol. Mine ore,
craft a supply chain, defend the base against critter waves, and build rockets to
ship **Luminite** into orbit. Install a life support unit on a rocket and you
leave with it.

Run it from the engine root:

```sh
bake run ore_else --local-env
```

The game opens on a title screen that sums up the goal (mine, defend, escape)
and lists the controls; press any key to start. `--stresstest` skips it.

For performance testing, `--stresstest` skips the early game and drops you
into a late-game base: two rings of walls with a solid ring of turrets behind
them, a drill on every deposit, a solar farm sized to the demand, a rocket pad,
factories and drone pads, a stocked inventory, and the clock advanced to 30
minutes of play so the first wave is the one you would face by then.
`--stresstest=45` picks a different number of minutes. Restarting (R) rebuilds
the same base.

```sh
bake run ore_else --local-env -- --stresstest
```

## Publishing to the web

```sh
bake3 projects/ore_else --local-env --target em --cfg release
```

Copy `ore_else.js` and `ore_else.wasm` from
`.bake/local_env/wasm32-Emscripten/release/bin/ore_else/` into
`SanderMertens.github.io/games/ore_else/` and set `EXPECTED` in that folder's
`index.html` to the new `.wasm` byte size. Build from a clean tree (a worktree
at the commit you want to ship), and remember that the flecs bundle is cloned
from the committed HEAD of `../../flecs`: uncommitted flecs changes never reach
the binary.

Always run the result in a browser before pushing; native builds do not catch
WGSL uniformity errors or wasm memory faults:

```sh
(cd path/to/games && python3 -m http.server 8765)
node tools/webcheck.mjs http://127.0.0.1:8765/ore_else/index.html /tmp/ore.png 60000 35000
```

It drives headless Chrome with WebGPU over CDP, prints console errors and
exceptions, clicks and presses Q at 35 s to leave the title screen, and
screenshots at 60 s. A clean run prints nothing but the input and done lines
and the screenshot shows the HUD. Building with `--profiling-funcs` in the
Emscripten link flags keeps function names in release stack traces when you
need to chase a crash.

## Goal

- Score = Luminite shipped to orbit. Cargo rockets ship it; a rocket with life
  support ships it *and* takes you off the planet (win).
- You lose when the swarm kills you. Shipping Luminite does not anger the
  planet - only the clock does.
- The waves grow forever. There is no last wave and no cap you can out-build
  indefinitely: ship as much Luminite as you can before the swarm becomes
  untenable.

## Controls

| Key | Action |
| --- | --- |
| W A S D | Walk (camera relative) |
| Shift (hold) | Run - twice the walk speed |
| Mouse wheel | Zoom: three camera levels, each further out and steeper |
| Right mouse (hold) | Work the tile under the cursor: mine a deposit, or demolish a building - the tile has to be within reach, the marker turns red when it is not |
| E | Open / close the inventory screen (inventory left, craft right) |
| Left click | Place the selected building - within the same reach you mine and demolish at |
| Left click a power bar | Set that category's level in the bottom-left **power allocator** (Mining / Weapons / Construction, 0-4 each, six bars to share); clicking the topmost lit bar switches the category off |
| Left click the quality button | Cycle the rendering quality at the top of the pause menu: **High** (default) - **Medium** - **Low** - and back to High |
| Left click + drag | Paint a line of buildings: ghosts follow the drag, nothing is built until you let go |
| Shift + drag | Fill a rectangle instead of a line |
| Q | Pipette: picks the building under the cursor for placement, or the drill when the cursor is on ore (needs one in your inventory); over empty ground it clears the selection |
| ESC | Cancel a placement drag, close the inventory screen, clear the selection - and when there is nothing left to back out of, pause / resume |
| R | Restart (on the win/lose screen) |
| Any key | Start (on the title screen) |
| Tab | Switch the Construct section between Buildings and Rocket |
| 1 - 6, 8, 0, 9 | Take a building out of the inventory to place: Wall, Drill, Solar, Gun, Laser, Missile, Rocket pad, Factory, Drone pad |
| F G H J | Craft: rocket engine, fuel tank, cargo bay, life support |
| Shift + click / hotkey | Order five instead of one |
| Click a queue row | Cancel one of the things you asked for |
| T Y U I | Install on the pad: engine, fuel tank, cargo bay, life support |
| O | Load Luminite into the rocket (200 per cargo bay) |
| P | Launch |

The hotkeys work with the screen closed too, so the screen is a menu you look
things up in, not a menu you have to live in. Hover a slot in the screen to see
what an item costs: the "Needs" row is priced in **ore**, all the way down the
recipe tree, and reads `have / need` per ore. The `need` is the whole cost; the
`have` counts the ore in your pack plus the ore already locked up in the plates
and wire on your shelf, so stocking parts fills the row in rather than shrinking
it. A row only turns red when you genuinely cannot get there - red means "go
mining, and this much", never "make some wire first". Picking a
building out of your inventory closes the screen so you can place right away;
crafting keeps it open so you can queue up a batch. The Construct side has two
tabs, Buildings and Rocket; **Tab** flips between them, and the craft hotkeys
work on either one. There are no buttons for the in-between parts - plates,
wire, chips - because you never have to ask for them: order the thing you
actually want and everything it needs is queued underneath it. The game keeps
running while the screen is up - the swarm does not wait.

**ESC is one step back at a time**: it throws away a placement drag, then closes
the screen, then clears the building you picked up - and when there is nothing
left to back out of, it pauses. Everything stops dead while paused, the swarm
included. ESC again resumes. E and R do nothing while paused, so you cannot open
the screen or restart out of it - unpause first.

## Building things

Buildings are items. You craft a wall the same way you craft an iron plate -
click it on the "Buildings" tab on the right, the whole chain of
prerequisites gets queued, and a wall lands in your inventory when it is done.
Placing it is a separate step: click the wall in your inventory (or press its
hotkey), the screen closes, and the next left click drops it on the map and
spends the item. Run out and the selection clears itself, so you never carry a
red ghost around.

**Drag to paint.** Hold the left button and sweep and you get a line of ghosts
from where you pressed - straight, axis-locked, turning one corner if you drag
diagonally (hold shift for a filled rectangle instead). Nothing is built while
you drag: let go and the whole line goes down at once, one item paid per
building, stopping quietly when you run out or a tile is blocked. ESC or the
right button during a drag throws the whole thing away.

**One reach for everything.** You build as far as you can mine and demolish - the
same distance, the same red marker when you are outside it. A big building is
judged by its nearest tile, so a rocket pad is not penalised for being three
tiles wide. Click somewhere you cannot reach and the game says "Too far away"; a
drag that runs out past your reach simply builds the part it can and skips the
rest.

Taking a building down uses the same right mouse you mine with: point at it and
hold. An **undamaged** building comes back as an item - you built it, you keep
it. A **damaged** one is salvaged instead: you get back the materials it was
made of, in proportion to the health it has left, and not the building itself. A
wall at half health returns half its bricks, rounded down; a turret at 10 % may
return nothing at all. So moving a turret by demolishing it and placing it again
costs you nothing but time only while it is intact - once the critters have chewed
on it, moving it costs you the difference. If the salvage would not fit in your
15 slots the doze is refused and the building stays standing.

## Inventory space

You have **15 slots and 15 kinds of things is the limit**. A slot holds any
amount of one kind, but a sixteenth *kind* does not fit anywhere:

- mining an ore you have no slot for yields nothing and leaves the deposit
  untouched,
- a craft that would deliver a new kind waits with its row red, saying
  "inventory full", until a slot frees up, and a cancel whose refund would not
  fit is refused,
- a demolish that would hand you a building you are not already carrying is
  refused, and the building stays standing.

The screen says "Inventory full" when this bites. Spend something - place a
building, install a rocket part, launch some Luminite - and the queue picks up
where it left off.

## Crafting

Ask for the thing you want, not for the parts. Clicking anything in the Collect
grid - an item or a building - queues the whole chain of prerequisites, deepest
first: click a rocket engine with nothing but ore in your pockets and you get
the iron plates and copper plates ahead of it, click a gun turret and you get
the wire and the chips.

**You pay when you order.** The moment you click, the whole chain is priced
against what you are actually holding and the materials leave your pockets - the
counts in the inventory drop immediately. If you cannot pay for the whole chain
right now, the slot is dim and the click does nothing. There is no ordering
against ore you hope to have later, and no way to promise the same ore twice: an
order you placed is an order you can afford.

A dim slot therefore means "not enough ore in your pockets", not "no plates in
stock" - a plate you do not own yet is never a reason to dim, because the queue
will make it. Order two engines and the second one dims once the first has taken
the ore.

**One click, one order.** The list in the bottom right corner is your order
book. Each thing you asked for is its own block: a bright header row with the
item and how many you want, and under it, indented, the parts being made for
*that* order. Orders never share parts. Order a laser turret and then a missile
turret and you get two blocks, each with its own plates, wire and chips - the
laser's parts are the laser's, and they cannot be quietly eaten by the missile.

**The oldest order goes first.** Workers always take the next thing they can
start from the earliest unfinished order, and only look further down the list
when that order has genuinely nothing to start. So the laser turret finishes,
then the missile turret starts - you get a working turret early instead of two
half-built ones. If you have several workers they pile onto the *same* order and
halve it, rather than each wandering off to a different one.

A row goes red when its order is stuck. Stuck almost always means "your
inventory is full" - the finished thing has nowhere to go - and the row says so
in as many words. Make room and it picks up where it left off. A red row never
means you are short on ore: the ore was paid for when you ordered.

**Click the header row to cancel one.** Only the header is clickable; the
indented part rows are just there to show you what is happening. Cancelling the
last one of an order removes that order **completely** - the header, every part
still queued for it, anything a worker had half-finished, and every scrap of ore
it was holding comes straight back to your pockets, all at once. Nothing is left
behind to clog the list. Parts that had already been finished and handed over
stay yours as spare parts. If the refund would not fit in your 15 slots the
cancel is refused and told you so, rather than losing the materials.

Lose a factory mid-job - dozed, or chewed up by critters - and the ore it was
holding goes back into its order, and another worker picks the job up.

**Shift+click for five.** Holding shift on a Collect slot (or on its hotkey)
orders five instead of one, stopping early when you run out - asking for five
drills with plates for three gets you three. Those five are **one** order of
five, so one click on its header takes one back off the top.

## Factories

You are a crafter, and so is a factory. Build one and it becomes a second pair
of hands: it watches the same queue, takes the first thing it can start, and
works it to completion. Two factories make three workers, and the queue drains
about three times as fast. They are not specialists - anything you can make by
hand, they can make.

A factory is 2x2, expensive (50 plates, 32 wire, 24 chips), and hungry: 50 W,
more than three solar panels make. Press **0** to take one out of your inventory.

Watch it work: the flywheels spin, the press comes down on the plate with a puff
of dust, the rollers turn and the stacks smoke. When it is powered but has
nothing to do, everything stops and the teal lamps stay on.

## Repair drones

Nothing else in the game gives health back. A **drone pad** is 2x2, costs 24
plates, 18 wire and 12 chips, draws 20 W, and parks four small drones. When
something within their range is damaged they take off, hover over it and weld it
back to full - and the scorch marks fade off it as they go, so you can watch a
chewed-up wall come clean.

They run on **iron**: one iron out of your pockets per 10 health repaired,
straight from the same crate you craft from. No iron, no drones - they sit on
the pad and wait. They also sit there through a blackout, like everything else
that needs power. Four drones from one pad never pile onto the same building,
so a pad behind your wall patches four sections at once. Press **9** to place
one.

## Power is all or nothing

**If demand ever exceeds production, the whole base blacks out.** Not a
slow-down - a stop. Every drill, factory and powered turret
freezes where it stands and does nothing at all until you fix it. The power
readout turns red the instant it happens, and every powered building's status
lamp flips from teal to a blinking red, so you can see which half of your base
is dead at a glance.

Nothing is lost while the lights are out. A drill keeps its progress toward the
next ore, a factory holds its half-finished item and picks it up mid-stroke when
the power comes back. Fix it by dozing something or building another panel, and
watch the base fade back in.

**You choose where the power goes.** The panel in the bottom-left corner splits
the base into Mining, Weapons and Construction, each on a bar of four, with six
bars to share between them - two each to start, which is the speed everything is
balanced around. Every step is half again: level 4 mines, shoots and builds at
double speed, level 1 at half, level 0 stops that side of the base entirely and
turns its lamps red. The bill follows the dial - a base running everything at
level 1 draws half the watts - so turning something down is a real way out of a
blackout, and "FREE n" tells you what you have left to spend. The Weapons dial
is a firing doctrine rather than a wattage bill, so it drives **every** turret -
the gun turret answers to it too, even though it draws no power of its own.

Two things keep working in a blackout: your **gun turrets**, which never wanted
power in the first place, and your own two hands - you can always craft.
Everything else stops. Laser and missile turrets draw power and **hold fire**
completely while the lights are out - not a slower rate, no shots at all. Do
not let the lights go out during a wave.

## Rendering quality

The button at the top of the pause menu sets how hard the renderer works. It starts on
**High**, which is the game exactly as it was authored, and each click steps one
notch down before wrapping back around.

| Tier | What changes |
| --- | --- |
| High | Nothing - full resolution, 4096 shadow map, ambient occlusion and bloom |
| Medium | Shadow map halved and ambient occlusion off: contact shading under rocks, crystals and buildings goes, shadow edges soften a little |
| Low | Everything Medium drops, plus half resolution, a quarter-size shadow map and no bloom: the world gets visibly softer and shadows blockier, the HUD stays sharp |

Exposure and tone mapping never switch off - they are what makes the picture
read at all, not decoration - so no tier looks washed out or blown.

The setting is not saved between runs: every new run starts on High.

## Rockets

A rocket is valid when every unit has its engines and fuel:

- units = cargo bays + life support (max 3 cargo bays, max 1 life support)
- engines = 4 per unit, fuel tanks = 1 per unit

The panel on the right appears once a pad exists and shows what is missing.
Launching a cargo-only rocket empties the pad so you can build the next one;
launching with life support ends the run.

## Tips

- Walk onto a wall or a turret and the jetpack lifts you over it - handy for
  getting out of your own maze. You can still mine and shoot from up there.
- Right mouse is aimed, not proximity based: point at the tile you want and
  hold. Switching tiles restarts the swing, so let a tile finish before you move
  on. A tile with a building on it demolishes instead of mines, which is how you
  take a drill off a deposit.
- Drills only go on deposits, and only run on power. One solar panel (10 W)
  feeds two drills (4 W each) - and a factory on its own needs a panel and a
  half, so build the panel first.
- Gun turrets draw no power: they shoot whatever comes into range, blackout or
  not. Laser and missile turrets do draw power, and go completely silent in a
  blackout - keep a panel spare if they are your wall.
- Missile turrets fire **homing missiles**: the missile steers after the critter
  it was fired at instead of flying where the critter used to be, and picks a new
  one nearby if its target dies on the way. It still explodes for splash damage,
  so a missile that chases one critter into a pack hurts the whole pack - they are
  your answer to the big, slow things a gun turret cannot chew through.
- Walls are targets, not obstacles: critters stop to chew them, which is what
  buys the turrets time. Build a funnel, not a fence.
- The first wave lands at 90 seconds and is two mites. Every wave after that
  is worth a little more than the last, forever.
- **Waves come in from the map edges**, one to four clusters per wave, and the
  directions change every time: a wave from the north-east, the next one from
  two sides at once. Do not build a wall on one side and call it a base.
- Each wave rolls a shape as well as a size: a **swarm** wave buries you in
  cheap bodies, an **elite** wave sends a handful of very expensive ones, and
  most waves are a mix. The same wave number is a different fight every run.
- The tiers unlock on a ladder: mites from wave 1, skitters from 4, brutes
  from 7 and **behemoths from 13** - a green, eight-legged, 1400 hp siege beast
  twice the size of a brute. One laser turret cannot kill one before it
  arrives; three can. It walks straight through a mite swarm and shoves it
  aside.
- Critters do not stack: they pack into a crowd and spread out along whatever
  they are chewing on, so a wall gets swarmed along its whole length.
- Buildings weather where they stand: they go up clean, dust settles on their
  tops over the first few minutes, grime creeps up their bases, and cloud
  shadows drift across the map. Nothing rusts through - but a building that has
  been chewed on **scorches**, and the soot deepens as its health drops, so you
  can read the state of your wall from across the base before you look at a
  single health bar. Heal it, by drone, and the soot lifts again.
- Damaged buildings carry a small health bar above them. An undamaged one shows
  nothing, so anything with a bar over it is something the critters got to.
- Dead critters leave charred, smoking husks for a few seconds before they sink
  into the sand. They are corpses, not cover - nothing shoots them and nothing
  hides behind them.
- The starting crate covers a drill, a solar panel and a gun turret with plates
  to spare - queue them first, mine after.
- Luminite deposits sit in the map corners, far from the base. Drills there are
  unprotected; a wall ring and a gun turret keep them alive.
- Rocket parts are expensive (a one-bay rocket costs 54 iron plates and 26
  copper plates). Get iron drills running long before you need them.
- Cargo bays are capacity, not a requirement. Launch a half-empty rocket if the
  base is about to fall - shipped Luminite is banked, Luminite in your pocket
  is not.

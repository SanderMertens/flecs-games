# Biome

A tiny RTS about terraforming a barren planet, built with
[flecs](https://github.com/SanderMertens/flecs) and the
[flecs engine](../flecs-engine).

<img width="1439" height="875" alt="Screenshot 2026-07-14 at 1 05 39 AM" src="https://github.com/user-attachments/assets/97f1226c-9c6a-4295-a6d2-5414935734be" />

# Run
Install [bake3](https://github.com/SanderMertens/bake3) by cloning the repository, and running this script in the bake3 repository root:

```bash
./setup.sh
```

Then run the game with the following command:

```bash
bake3 run biome --cfg release --local-env
```

When developing, run in debug mode:

```bash
bake3 run biome --local-env
```

To debug memory issues, run in sanitized mode:

```bash
bake3 run biome --local-env
```

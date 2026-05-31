# Undecedent Core Principles

Undecedent is an original, ultra-fast wave-survival shooter with a first-class in-engine map editor. It draws energy from classic arcade survival pacing and Build-era editing immediacy, but it must stand on its own identity, rules, fiction, assets, names, and maps.

This document is the project north star. When implementation choices compete, prefer the option that keeps the game faster, clearer, more playable, and easier to edit.

## Original Inspiration

- Be inspired by the feel of escalating wave survival, map mastery, pressure economies, and desperate last-second decisions.
- Do not copy Call of Duty names, lore, weapons, perks, enemies, audio, UI, level layouts, iconography, or exact mechanics.
- Build a distinct fiction and ruleset early enough that every feature feels native to Undecedent rather than like a placeholder clone.
- Treat references as design prompts, not targets to reproduce.

## IP Guardrails

- Treat broad genre conventions as available design territory: survival waves, escalating pressure, resource scarcity, routes, locked spaces, enemy readability, spatial mastery, and arcade pacing.
- Treat specific expression as off-limits: recognizable characters, factions, storylines, named systems, exact perk/economy structures, weapon identities, sound cues, UI trade dress, memorable room layouts, set pieces, iconography, and marketing language from other games.
- Use the scenes a faire idea carefully. Common or necessary genre elements can inspire Undecedent, but the project must add original expression, combinations, presentation, fiction, tuning, and implementation.
- When a feature feels too close to a reference, rename it, reshape its rules, change its presentation, and document the Undecedent-specific purpose before it ships.
- Prefer internal reference language like "wave pressure," "route mastery," or "arcade survival economy" over brand-specific shorthand in code, assets, docs, and UI.
- This document is a creative and engineering guardrail, not legal advice. If a planned feature depends on close imitation of another game's expression, get legal review or redesign it.

## Speed First

- The game should feel immediate: high framerate, low input latency, fast startup, fast reloads, and short edit-to-play loops.
- Prefer simple hot paths over clever abstractions. A system that is easy to profile, reason about, and rewrite is better than one that hides cost.
- Keep runtime features scalable under stress: many enemies, many projectiles, dynamic lighting cues, audio pressure, and editor overlays must not collapse the frame.
- Profile before adding complexity, and keep performance measurements close to the features they justify.
- Make slow work visible. Loading, validation, generation, and rebuilds should report what they are doing instead of freezing silently.

## Gameplay Feel

- Combat should be readable at full speed. Enemy silhouettes, hit feedback, damage states, pickups, doors, traps, and hazards must communicate instantly.
- Movement and aiming should be tight, predictable, and responsive before they are flashy.
- Maps should create meaningful spatial decisions: routes, holds, loops, risks, shortcuts, locks, reveals, and retreat options.
- Economy pressure should force interesting tradeoffs without becoming accounting homework.
- Spectacle is welcome when it is performance-friendly and reinforces play clarity.
- Every mechanic must prove itself in a playable slice. If it does not improve moment-to-moment tension, flow, or mastery, cut it or simplify it.

## Editor Immediacy

- The editor is part of the engine, not an external afterthought.
- Favor Build-like sector editing: quick layout changes, fast height and portal adjustments, direct tagging, and geometry that can be understood at a glance.
- Make the edit/play loop instant. Designers should be able to change a room, jump in, test a wave route, and return without ceremony.
- Support keyboard-heavy workflows, precise numeric edits, and direct manipulation. Mouse tools should be fast; keyboard tools should be faster.
- Keep game data visible where it matters: spawn logic, doors, triggers, nav constraints, economy zones, lighting intent, and validation errors.
- Minimize modal UI. The editor should keep the map, selection, and current tool in view as much as possible.
- Validate live. Broken sectors, missing links, unreachable spawns, invalid triggers, and expensive geometry should be caught while editing.

## Engine Architecture

- Use simple, data-oriented structures for core runtime systems: world geometry, entities, enemies, projectiles, collision, triggers, and rendering batches.
- Share formats between editor and runtime whenever practical. Avoid conversion pipelines that make iteration slow or hide the real game state.
- Keep systems deterministic-feeling where it supports debugging, replays, testing, and editor validation.
- Prefer explicit ownership and lifetime rules over magic global behavior.
- Keep dependencies intentional. Add libraries for proven value, not novelty.
- C++23, SDL3, OpenGL, and GLAD are the current foundation; principles should guide this stack without requiring an immediate rewrite.

## Project Culture

- Build playable vertical slices before speculative architecture.
- Keep the smallest useful version of each system working, measurable, and replaceable.
- Write code that future editing tools can inspect and explain.
- Preserve fast local builds and simple launch paths.
- Treat bugs in input latency, frame pacing, editor data loss, and map corruption as high-priority issues.
- When tradeoffs conflict, choose in this order: player responsiveness, editor iteration speed, gameplay clarity, maintainable implementation, visual flourish.

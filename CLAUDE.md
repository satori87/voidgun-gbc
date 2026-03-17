# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

voidgun-gbc — A Game Boy Color remake of Voidgun (a multiplayer 2D space combat game). The original game (at C:/dev/spacedout) is a Java/LibGDX networked shooter with 3 ship classes, 10+ weapons, and FFA/TDM/CTF modes. This GBC version adapts those mechanics to the Game Boy Color's constraints.

## Build System

GBDK-2020 v4.5.0 is installed at `C:/gbdk`.

Compile a ROM:
```
C:/gbdk/bin/lcc -Wa-l -Wl-m -Wl-j -o output.gb source.c
```

Asset conversion (PNG to GBC tile data):
```
C:/gbdk/bin/png2asset input.png -o output.c
```

## Original Game Reference

The original Voidgun source is at `C:/dev/spacedout` (Java/LibGDX). Key design elements to adapt:
- **Ship classes:** Light (fast, low HP), Medium (balanced), Heavy (slow, high HP)
- **Weapons:** Various fire rates, bullet speeds, spread patterns, AOE
- **Game modes:** Free-for-all, Team Deathmatch, Capture the Flag
- **Physics:** Zero-gravity thrust-based movement with rotation
- **Resolution:** Original is 960x540; GBC is 160x144

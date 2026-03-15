# AGENTS.md

## Purpose

This repository contains firmware and supporting docs for DIY audio projects using:
- ESP32 modules
- ATtiny412
- ATtiny1616

Main goals:
- generate compile-ready embedded code
- keep hardware assumptions explicit
- preserve known-good behaviour unless explicitly told to change it
- prefer robustness and clarity over clever abstractions

## Repository layout

- `esp32/` — ESP32-based firmware projects
- `ESP8266/` — ESP8266-based firmware projects
- `attiny/412/` — ATtiny412 projects
- `attiny/1616/` — ATtiny1616 projects
- `docs/` — shared documentation such as pin maps, behaviour notes, and coding rules

Create separate folders for each real project under the relevant MCU folder.

Examples:
- `esp32/preamp_controller/`
- `attiny/1616/relay_mute_controller/`
- `attiny/1616/input_selector/`
- `attiny/412/low_battery_monitor/`

## Project structure expectations

For each project, include:
- `README.md` — summary, MCU, framework, pins, behaviour, status
- source files in a layout suitable for the toolchain used
- optional local docs for hardware notes or protocol details

For Arduino-style projects, prefer simple project layouts that match the Arduino toolchain.
Do not over-engineer folder structures unless the project genuinely needs it.

## Default assumptions

Unless explicitly told otherwise:
- ESP32 projects use Arduino framework
- ATtiny projects use megaTinyCore when applicable
- code should be suitable for hobbyist embedded development
- code should be easy to understand and modify by hand
- behaviour should be deterministic and conservative on startup

## Coding rules

- Always include a clear pin map near the top of each firmware file.
- Always comment GPIO assignments and important constants.
- Prefer straightforward C/C++ suitable for embedded targets.
- Avoid unnecessary abstraction, metaprogramming, or large frameworks.
- Prefer readable state machines over tangled flag logic.
- Prefer explicit constants over unexplained magic numbers.
- Keep RAM and flash usage in mind for small MCUs.
- Avoid dynamic memory allocation unless explicitly justified.
- Avoid blocking delays when a non-blocking approach is clearly better, but use simple delays when appropriate for tiny, low-complexity tasks.
- Keep serial debug optional and easy to disable.

## Hardware safety and behavioural rules

- Default to safe startup states.
- Outputs that control relays, mute, motors, or power-related functions must initialize to a safe inactive state unless explicitly specified otherwise.
- Do not assume undocumented pull-ups, pull-downs, or level shifters exist.
- Do not invent hardware features that are not in the project README or task prompt.
- If hardware details are missing, make the minimum reasonable assumptions and clearly state them in comments or README updates.

## Change control rules

- Preserve existing working behaviour unless the task explicitly requests behavioural changes.
- Do not rename pins, files, variables, or interfaces without a clear reason.
- Do not rewrite unrelated parts of a project while fixing a small issue.
- When editing existing code, keep the style and structure consistent unless the user asks for a refactor.

## User-specific rules

These rules are important and should not be changed unless explicitly requested:

- Do not change volume-control behaviour or input-selector logic in existing preamp-related code unless explicitly asked.
- Preserve existing volume taper / mapping behaviour when working on the preamp project unless the task explicitly requests a new curve.
- Treat previous working logic as intentional unless there is strong evidence of a bug.

## Output expectations

When creating or updating firmware:
- produce compile-ready code, not pseudocode
- include a short header comment explaining the project
- include a pin map
- include brief usage notes if setup is non-obvious
- keep comments useful and not excessive

When creating a new project folder:
- add or update that project's `README.md`
- document assumptions
- document any external libraries required
- document build/upload expectations if known

## Preferred prompt interpretation

When given a task, interpret it using this priority:
1. follow explicit user instructions
2. follow project-level AGENTS.md guidance closest to the target folder
3. follow this root AGENTS.md
4. make the simplest reasonable implementation choices

## Definition of done

A task is done when:
- the requested code or edits are present
- the result is internally consistent
- pin assignments and assumptions are documented
- the code is as close to compile-ready as the available information allows
- unrelated behaviour has not been changed unnecessarily

## When uncertain

If requirements are ambiguous:
- prefer small, conservative changes
- document assumptions in comments or README
- avoid inventing extra features

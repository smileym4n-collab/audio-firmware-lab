# AGENTS.md

## Purpose

This repository contains DIY embedded firmware projects using:
- ESP32
- ESP8266
- ATtiny412
- ATtiny1616

Main goals:
- generate compile-ready firmware
- preserve known-good behaviour unless explicitly asked to change it
- keep implementations simple and readable
- avoid unnecessary abstractions

---

## Default assumptions

Unless explicitly stated otherwise:
- ESP32 projects use Platformio framework
- ATtiny projects use megaTinyCore
- code is intended for hobby embedded development
- startup behaviour should default to safe states

---

## Coding rules

- Keep changes small and focused.
- Do not rewrite unrelated code.
- Prefer straightforward embedded C/C++.
- Avoid unnecessary classes, frameworks, or abstractions.
- Prefer readable state machines over complicated flag logic.
- Keep RAM/flash usage reasonable for small MCUs.
- Avoid dynamic allocation unless clearly necessary.
- Use non-blocking code where useful, but simple delays are acceptable for tiny tasks.
- Keep serial debug easy to disable.

---

## Hardware rules

- Always initialise outputs to safe states.
- Do not invent undocumented hardware features.
- Do not assume pull-ups, pull-downs, or level shifters exist unless documented.
- Preserve existing pin assignments unless explicitly asked to change them.

---

## Existing project protection

- Preserve existing behaviour unless explicitly asked to change it.
- Treat existing logic as intentional unless clearly broken.
- Do not change volume curves, input selection behaviour, or relay timing in preamp projects unless requested.

---

## Firmware expectations

When modifying firmware:
- produce compile-ready code
- include clear GPIO/pin comments
- keep comments concise and useful
- document assumptions in comments when needed

---

## Project structure

Typical layout:

- `esp32/`
- `ESP8266/`
- `attiny/412/`
- `attiny/1616/`

Keep Arduino-style projects simple.
Do not create complex folder structures unless genuinely needed.

---

## Versioning

Only update versions or changelogs when explicitly requested.

For `PreAmpv2.ino`:
- preserve the existing version header format
- increment patch version when making firmware changes

---

## Preferred behaviour

When uncertain:
- prefer minimal changes
- avoid adding extra features
- avoid speculative refactors
- keep implementations conservative and easy to debug

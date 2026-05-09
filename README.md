# Vision-Guided Autonomous Forklift

This repository contains a mechatronics project for a **vision-guided autonomous material handling robot** (student prototype) that follows a guided path, classifies product color, and delivers to the correct dock using a fork-lifter mechanism.

## Project Focus

- **Guided navigation:** IR line-sensor based path following
- **Vision decision layer:** camera + OpenCV color classification (red/green/blue)
- **Control architecture:** Raspberry Pi (high-level mission logic) + Arduino Mega (real-time control)
- **Material handling:** servo-driven fork-lifter pickup and drop
- **Safety:** ultrasonic obstacle/docking checks

## Repository Structure

```text
Autonomous Forklift/
├─ Proposal/
│  └─ autonomous_material_handling_robot_proposal_fixed.pdf
├─ Simulation/
│  └─ simulation.py
└─ CAD/
   └─ GrabCAD/   (component CAD models and reference assets)
```

## Proposal (Main Reference)

The proposal in `Proposal/autonomous_material_handling_robot_proposal_fixed.pdf` defines:

- Abstract, objectives, and full system architecture
- Operational flow and control loops
- Power/signal distribution strategy
- Suggested pin allocation and software module plan
- Bill of materials and estimated cost range
- Risks, mitigation plan, expected outcomes, and future scope

## Simulation

`Simulation/simulation.py` is a Tkinter visual simulation of the proposed workflow:

- Mission-based pickup → classify → dock → return cycle
- Route movement over a guided lane map
- Randomized obstacle events and handling pauses
- Classification uncertainty/misclassification behavior
- Success/failure tracking across missions

### Run the simulation

```bash
python Simulation/simulation.py
```

Optional arguments:

```bash
python Simulation/simulation.py --missions 10 --seed 42
```

## CAD Assets

`CAD/GrabCAD/` includes mechanical and electronics reference models (e.g., motors, servos, Raspberry Pi, Arduino Mega, sensors, wheels, and motor driver files) used for hardware design and integration planning.

---

If you are reviewing this repo on GitHub, start with the **Proposal PDF** for complete project context, then run the **Simulation** for a visual behavior walkthrough.

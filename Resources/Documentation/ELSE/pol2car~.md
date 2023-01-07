---
title: pol2car~

description: Polar to cartesian conversion

categories:
- object

pdcategory:

arguments:
- description:
  type:
  default:

inlets:
  1st:
  - type: float/signal
    description: amplitude of the signal in the polar form
  2nd:
  - type: float/signal
    description: phase (in radians) of the signal in the polar form

outlets:
  1st:
  - type: signal
    description: real part of the signal in the cartesian form
  2nd:
  - type: signal
    description: imaginary part of the signal in the cartesian form

draft: false
---

[pol2car~] converts polar coordinates (amplitude / phase) to cartesian coordinates (real / imaginary).

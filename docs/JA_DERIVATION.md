# Jiles-Atherton Derivation Used By `HysteresisModel`

This note fixes the convention used by the scalar Jiles-Atherton solver.
It is intentionally narrow: it documents the equation implemented in
`core/include/core/magnetics/HysteresisModel.h`, not a full transformer model.

References:

- D. C. Jiles and D. L. Atherton, "Theory of ferromagnetic hysteresis",
  Journal of Magnetism and Magnetic Materials 61, 48-60, 1986,
  DOI: `10.1016/0304-8853(86)90066-1`.
- M. Szewczyk et al., "Harmonization and Validation of Jiles-Atherton Static
  Hysteresis Models", Energies 15(18), 6760, 2022.

## State Convention

The solver state `M` is the total magnetization returned by the model. It is
not a separate `M_irr` state.

The effective field is:

```text
Heff = H + alpha*M
```

The anhysteretic magnetization is:

```text
Man = Ms * L(Heff/a)
D   = dMan/dHeff
```

The irreversible branch is:

```text
A = (Man - M) / (delta*k - alpha*(Man - M))
```

with the usual direction gate:

```text
A = 0 when delta*(Man - M) <= 0
```

The code parameter `c` is the modern reversible ratio in `[0, 1]`. It is not
the older paper notation where factors appear as `1/(1+c)` and `c/(1+c)`.

## Chain Rule

The reversible branch explicitly depends on `Man(Heff)`, so:

```text
dMan/dH = D * dHeff/dH
        = D * (1 + alpha*dM/dH)
```

Using `chi = dM/dH`, the implemented equation is:

```text
chi = (1-c)*A + c*D*(1 + alpha*chi)
```

Solving for `chi`:

```text
chi * (1 - alpha*c*D) = (1-c)*A + c*D

chi = ((1-c)*A + c*D) / (1 - alpha*c*D)
```

The important point is that the denominator is **not**
`1 - alpha*((1-c)*A + c*D)`. Applying the chain factor to the full sum would
feed `alpha` back through the irreversible branch even though `A` already
contains the Jiles-Atherton mean-field denominator.

## Sanity Limits

The implementation must satisfy:

```text
alpha = 0 -> chi = (1-c)*A + c*D
c = 0     -> chi = A
H=M=0     -> chi = (c*Ms/(3a)) / (1 - alpha*c*Ms/(3a))
```

The analytical Jacobian differentiates:

```text
N = (1-c)*A + c*D
Q = 1 - alpha*c*D
chi = N/Q
```

so:

```text
dchi/dM = (dN/dM * Q - N * dQ/dM) / Q^2
dQ/dM   = -alpha*c*dD/dM
dD/dM   = d2Man/dHeff2 * alpha
```

The derivative of `A` uses the same denominator epsilon and clamp rules as
`computeRHS()`. When the clamp is active, `dA/dM` is set to zero.
